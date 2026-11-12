#include <QAbstractListModel>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileSystemWatcher>
#include <QFileInfo>
#include <QBuffer>
#include <QGuiApplication>
#include <QMimeData>
#include <QImage>
#include <QIcon>
#include <QLocale>
#include <QModelIndex>
#include <QPixmap>
#include <QProcess>
#include <QQuickWindow>
#include <QScreen>
#include <QSaveFile>
#include <QRegularExpression>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QSettings>
#include <QSet>
#include <QSize>
#include <QSortFilterProxyModel>
#include <QStandardPaths>
#include <QTextStream>
#include <QTextLayout>
#include <QTextOption>
#include <QFontMetrics>
#include <QTimer>
#include <QVector>
#include <QUrl>
#include <X11/XKBlib.h>
#include <pwd.h>
#include <unistd.h>
#undef Bool
#include <algorithm>

namespace {

class KeyboardLayoutState final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QString name READ name NOTIFY nameChanged)

public:
	explicit KeyboardLayoutState(const QString &directory, QObject *parent = nullptr)
		: QObject(parent), m_directory(directory)
	{
		m_watcher.addPath(m_directory);
		connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
			this, &KeyboardLayoutState::refresh);
		refresh();
	}

	QString name() const { return m_name; }

signals:
	void nameChanged();

private slots:
	void refresh()
	{
		const QStringList layouts = QDir(m_directory).entryList(
			{QStringLiteral("layout-*")}, QDir::Files, QDir::Name);
		const QString name = layouts.isEmpty()
			? QStringLiteral("us") : layouts.first().mid(7);
		if (name == m_name)
			return;
		m_name = name;
		emit nameChanged();
	}

private:
	QString m_directory;
	QString m_name;
	QFileSystemWatcher m_watcher;
};

class PowerMenuState final : public QObject {
	Q_OBJECT
	Q_PROPERTY(bool open READ open NOTIFY openChanged)

public:
	explicit PowerMenuState(const QString &stateDirectory, QObject *parent = nullptr)
		: QObject(parent), m_stateFile(stateDirectory + QStringLiteral("/power-menu-open"))
	{
		m_watcher.addPath(stateDirectory);
		connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
			this, &PowerMenuState::refresh);
		refresh();
	}

	bool open() const { return m_open; }

public slots:
	void close()
	{
		QFile::remove(m_stateFile);
		setOpen(false);
	}

	void triggerAction(const QString &action)
	{
		if (action == QStringLiteral("logout")) {
			const QString sessionId = qEnvironmentVariable("XDG_SESSION_ID");
			if (!sessionId.isEmpty())
				QProcess::startDetached(QStringLiteral("loginctl"),
					{QStringLiteral("terminate-session"), sessionId});
		} else if (action == QStringLiteral("poweroff")) {
			QProcess::startDetached(QStringLiteral("systemctl"),
				{QStringLiteral("poweroff")});
		} else if (action == QStringLiteral("restart")) {
			QProcess::startDetached(QStringLiteral("systemctl"),
				{QStringLiteral("reboot")});
		}
	}

signals:
	void openChanged();

private slots:
	void refresh() { setOpen(QFile::exists(m_stateFile)); }

private:
	void setOpen(bool open)
	{
		if (m_open == open)
			return;
		m_open = open;
		emit openChanged();
	}

	QString m_stateFile;
	bool m_open = false;
	QFileSystemWatcher m_watcher;
};

class LockScreenState final : public QObject {
	Q_OBJECT
	Q_PROPERTY(bool locked READ locked NOTIFY lockedChanged)
	Q_PROPERTY(bool verifying READ verifying NOTIFY verifyingChanged)
	Q_PROPERTY(bool inputGrabbed READ inputGrabbed NOTIFY inputGrabbedChanged)

public:
	LockScreenState(const QString &stateDirectory, const QString &verifierPath,
		const QString &username, QObject *parent = nullptr)
		: QObject(parent), m_lockFile(stateDirectory + QStringLiteral("/lock-screen-open")),
		  m_backgroundFile(stateDirectory + QStringLiteral("/lock-screen-background.png")),
		  m_verifierPath(verifierPath), m_username(username)
	{
		m_watcher.addPath(stateDirectory);
		connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
			this, &LockScreenState::refresh);
		connect(&m_verifier, &QProcess::started, this, [this] {
			QByteArray payload = m_pendingPassword;
			m_pendingPassword.fill('\0');
			m_pendingPassword.clear();
			payload.append('\n');
			m_verifier.write(payload);
			m_verifier.closeWriteChannel();
			payload.fill('\0');
		});
		connect(&m_verifier, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
			this, &LockScreenState::verificationFinished);
		connect(&m_verifier, &QProcess::readyReadStandardError, this, [this] {
			const QByteArray detail = m_verifier.readAllStandardError().trimmed();
			if (!detail.isEmpty())
				qWarning().noquote() << "VXWM lock authentication:" << QString::fromLocal8Bit(detail);
		});
		connect(&m_verifier, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
			if (error != QProcess::FailedToStart)
				return;
			qWarning() << "Could not start VXWM PAM verifier:" << m_verifier.errorString();
			clearPassword();
			setVerifying(false);
			emit authenticationFailed();
		});
		refresh();
	}

	~LockScreenState() override
	{
		if (m_locked)
			setShortcutDaemonPaused(false);
	}

	bool locked() const { return m_locked; }
	bool verifying() const { return m_verifying; }
	bool inputGrabbed() const { return m_inputGrabbed; }

public slots:
	void verifyPassword(const QString &password)
	{
		if (!m_locked || m_verifying || password.isEmpty())
			return;
		if (m_username.isEmpty()) {
			qWarning() << "Could not determine the current user for PAM authentication";
			emit authenticationFailed();
			return;
		}
		m_pendingPassword = password.toUtf8();
		setVerifying(true);
		const QString pamConfigDirectory = QFileInfo(m_verifierPath).absolutePath()
			+ QStringLiteral("/pam.d");
		// pam_unix uses unix_chkpwd; a shell-launched UI can inherit NoNewPrivileges.
		m_verifier.start(QStringLiteral("systemd-run"), {
			QStringLiteral("--quiet"), QStringLiteral("--user"),
			QStringLiteral("--pipe"), QStringLiteral("--wait"),
			QStringLiteral("--collect"),
			QStringLiteral("--property=NoNewPrivileges=no"),
			m_verifierPath, m_username, pamConfigDirectory
		});
	}

	void closeLockScreen()
	{
		if (!m_locked)
			return;
		if (m_verifier.state() != QProcess::NotRunning)
			m_verifier.kill();
		clearPassword();
		setVerifying(false);
		QFile::remove(m_lockFile);
		setLocked(false);
	}

	bool grabInput(QObject *windowObject)
	{
		auto *window = qobject_cast<QQuickWindow *>(windowObject);
		if (!window || !m_locked || !window->isVisible())
			return false;

		window->raise();
		if (!window->isActive())
			window->requestActivate();
		if (m_inputGrabbed)
			return true;
		const bool keyboardGrabbed = window->setKeyboardGrabEnabled(true);
		const bool mouseGrabbed = window->setMouseGrabEnabled(true);
		const bool grabbed = keyboardGrabbed && mouseGrabbed;
		if (!grabbed) {
			window->setKeyboardGrabEnabled(false);
			window->setMouseGrabEnabled(false);
		}
		setInputGrabbed(grabbed);
		return grabbed;
	}

	void releaseInput(QObject *windowObject)
	{
		auto *window = qobject_cast<QQuickWindow *>(windowObject);
		if (window) {
			window->setKeyboardGrabEnabled(false);
			window->setMouseGrabEnabled(false);
		}
		setInputGrabbed(false);
	}

	void cycleKeyboardLayout()
	{
		Display *display = XOpenDisplay(nullptr);
		if (!display)
			return;
		XkbStateRec state;
		XkbDescPtr keyboard = XkbGetKeyboard(display, XkbAllComponentsMask,
			XkbUseCoreKbd);
		if (keyboard && XkbGetControls(display, XkbAllControlsMask, keyboard) == Success &&
			keyboard->ctrls && keyboard->ctrls->num_groups > 1 &&
			XkbGetState(display, XkbUseCoreKbd, &state) == Success) {
			const unsigned int nextGroup = (state.group + 1) % keyboard->ctrls->num_groups;
			XkbLockGroup(display, XkbUseCoreKbd, nextGroup);
			XSync(display, False);
		}
		if (keyboard)
			XkbFreeKeyboard(keyboard, XkbAllComponentsMask, True);
		XCloseDisplay(display);
	}

signals:
	void lockedChanged();
	void verifyingChanged();
	void inputGrabbedChanged();
	void authenticationFailed();

private slots:
	void refresh() { setLocked(QFile::exists(m_lockFile)); }

	void verificationFinished(int exitCode, QProcess::ExitStatus status)
	{
		if (status != QProcess::NormalExit || exitCode != 0)
			qWarning() << "VXWM PAM verifier failed with exit code" << exitCode
				<< "and status" << status;
		clearPassword();
		setVerifying(false);
		if (!m_locked)
			return;
		if (status == QProcess::NormalExit && exitCode == 0) {
			QFile::remove(m_lockFile);
			setLocked(false);
		} else {
			emit authenticationFailed();
		}
	}

private:
	void clearPassword()
	{
		m_pendingPassword.fill('\0');
		m_pendingPassword.clear();
	}

	void setLocked(bool locked)
	{
		if (m_locked == locked)
			return;
		setShortcutDaemonPaused(locked);
		m_locked = locked;
		emit lockedChanged();
		if (!locked) {
			QTimer::singleShot(500, this, [path = m_backgroundFile] {
				QFile::remove(path);
			});
		}
	}

	static void setShortcutDaemonPaused(bool paused)
	{
		QProcess::execute(QStringLiteral("pkill"),
			{paused ? QStringLiteral("-STOP") : QStringLiteral("-CONT"),
				QStringLiteral("-x"), QStringLiteral("sxhkd")});
	}

	void setVerifying(bool verifying)
	{
		if (m_verifying == verifying)
			return;
		m_verifying = verifying;
		emit verifyingChanged();
	}

	void setInputGrabbed(bool grabbed)
	{
		if (m_inputGrabbed == grabbed)
			return;
		m_inputGrabbed = grabbed;
		emit inputGrabbedChanged();
	}

	QString m_lockFile;
	QString m_backgroundFile;
	QString m_verifierPath;
	QString m_username;
	QByteArray m_pendingPassword;
	QProcess m_verifier;
	QFileSystemWatcher m_watcher;
	bool m_locked = false;
	bool m_verifying = false;
	bool m_inputGrabbed = false;
};

class AudioVolume final : public QObject {
	Q_OBJECT
	Q_PROPERTY(int volume READ volume NOTIFY volumeChanged)
	Q_PROPERTY(bool muted READ muted NOTIFY mutedChanged)
	Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)

public:
	explicit AudioVolume(const QString &stateDirectory, QObject *parent = nullptr)
		: QObject(parent), m_stateDirectory(stateDirectory)
	{
		connect(&m_subscriber, &QProcess::readyReadStandardOutput, this, [this] {
			m_monitorBuffer += QString::fromLocal8Bit(m_subscriber.readAllStandardOutput());
			static const QRegularExpression eventStart(
				QStringLiteral("(?m)^(?:added|changed|removed):"));
			auto match = eventStart.match(m_monitorBuffer);
			while (match.hasMatch()) {
				if (m_lastEventStart >= 0) {
					const QString event = m_monitorBuffer.mid(m_lastEventStart,
						match.capturedStart() - m_lastEventStart);
					if (event.contains(QStringLiteral("type: PipeWire:Interface:Node")) &&
						event.contains(QStringLiteral("media.class = \"Audio/Sink\"")))
						m_refreshTimer.start();
				}
				m_lastEventStart = match.capturedStart();
				match = eventStart.match(m_monitorBuffer, match.capturedEnd());
			}
			if (m_lastEventStart > 0) {
				m_monitorBuffer.remove(0, m_lastEventStart);
				m_lastEventStart = 0;
			}
		});
		connect(&m_query, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
			this, [this](int exitCode, QProcess::ExitStatus) {
			if (exitCode == 0) {
				static const QRegularExpression pattern(QStringLiteral("Volume:\\s*([0-9.]+)"));
				const QString output = QString::fromLocal8Bit(m_query.readAllStandardOutput());
				const auto match = pattern.match(output);
				if (match.hasMatch())
					setVolumeValue(qBound(0, qRound(match.captured(1).toDouble() * 100.0), 100));
				setMutedValue(output.contains(QStringLiteral("[MUTED]")));
			}
			setReady();
			if (m_refreshPending) {
				m_refreshPending = false;
				refresh();
			}
		});
		m_refreshTimer.setSingleShot(true);
		m_refreshTimer.setInterval(80);
		connect(&m_refreshTimer, &QTimer::timeout, this, &AudioVolume::refresh);
		m_subscriber.start(QStringLiteral("pw-mon"));
		refresh();
	}

	int volume() const { return m_volume; }
	bool muted() const { return m_muted; }
	bool ready() const { return m_ready; }

public slots:
	void setVolume(int value)
	{
		const int bounded = qBound(0, value, 100);
		QProcess::startDetached(QStringLiteral("wpctl"),
			{QStringLiteral("set-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@"),
				QString::number(bounded / 100.0, 'f', 2)});
	}

signals:
	void volumeChanged();
	void mutedChanged();
	void readyChanged();

private:
	void refresh()
	{
		if (m_query.state() != QProcess::NotRunning) {
			m_refreshPending = true;
			return;
		}
		m_query.start(QStringLiteral("wpctl"),
			{QStringLiteral("get-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@")});
	}

	void setVolumeValue(int value)
	{
		if (m_volume == value)
			return;
		m_volume = value;
		emit volumeChanged();
	}

	void setMutedValue(bool muted)
	{
		const QString markerPath = m_stateDirectory + QStringLiteral("/audio-muted");
		if (muted && !QFile::exists(markerPath)) {
			QFile marker(markerPath);
			if (marker.open(QIODevice::WriteOnly))
				marker.close();
		} else if (!muted) {
			QFile::remove(markerPath);
		}
		if (m_muted == muted)
			return;
		m_muted = muted;
		emit mutedChanged();
	}

	void setReady()
	{
		if (m_ready)
			return;
		m_ready = true;
		emit readyChanged();
	}

	int m_volume = 0;
	bool m_muted = false;
	bool m_ready = false;
	bool m_refreshPending = false;
	QString m_stateDirectory;
	QProcess m_subscriber;
	QProcess m_query;
	QTimer m_refreshTimer;
	QString m_monitorBuffer;
	qsizetype m_lastEventStart = -1;
};

class BrightnessControl final : public QObject {
	Q_OBJECT
	Q_PROPERTY(int brightness READ brightness NOTIFY brightnessChanged)
	Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)

public:
	explicit BrightnessControl(const QString &stateDirectory, QObject *parent = nullptr)
		: QObject(parent), m_stateFile(stateDirectory + QStringLiteral("/brightness-state"))
	{
		connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] {
			readState();
			watchStateFile();
		});
		connect(&m_query, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
			this, [this](int exitCode, QProcess::ExitStatus) {
			if (exitCode == 0) {
				static const QRegularExpression pattern(
					QStringLiteral("VCP\\s+10\\s+C\\s+(\\d+)\\s+(\\d+)"));
				const auto match = pattern.match(QString::fromLocal8Bit(m_query.readAllStandardOutput()));
				if (match.hasMatch()) {
					m_maxBrightness = qMax(1, match.captured(2).toInt());
					setBrightnessValue(qBound(0,
						qRound(match.captured(1).toInt() * 100.0 / m_maxBrightness), 100));
					writeState();
				}
			}
			setReady();
		});
		connect(&m_setter, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
			this, [this](int, QProcess::ExitStatus) {
			if (m_pendingBrightness < 0)
				return;
			const int pending = m_pendingBrightness;
			m_pendingBrightness = -1;
			startSet(pending);
		});
		readState();
		watchStateFile();
		refresh();
	}

	int brightness() const { return m_brightness; }
	bool ready() const { return m_ready; }

public slots:
	void setBrightness(int value)
	{
		const int bounded = qBound(0, value, 100);
		setBrightnessValue(bounded);
		writeState();
		if (m_setter.state() == QProcess::NotRunning)
			startSet(bounded);
		else
			m_pendingBrightness = bounded;
	}

signals:
	void brightnessChanged();
	void readyChanged();

private:
	void refresh()
	{
		if (m_query.state() == QProcess::NotRunning)
			m_query.start(QStringLiteral("ddcutil"),
				{QStringLiteral("getvcp"), QStringLiteral("10"), QStringLiteral("--brief")});
	}

	void startSet(int value)
	{
		const int rawValue = qRound(value * m_maxBrightness / 100.0);
		m_setter.start(QStringLiteral("ddcutil"),
			{QStringLiteral("setvcp"), QStringLiteral("10"), QString::number(rawValue)});
	}

	void readState()
	{
		QFile state(m_stateFile);
		if (!state.open(QIODevice::ReadOnly))
			return;
		bool ok = false;
		const int value = QString::fromLatin1(state.readAll()).trimmed().toInt(&ok);
		if (ok)
			setBrightnessValue(qBound(0, value, 100));
	}

	void writeState()
	{
		QFile state(m_stateFile);
		if (state.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			state.write(QByteArray::number(m_brightness));
			state.close();
		}
		watchStateFile();
	}

	void watchStateFile()
	{
		if (QFile::exists(m_stateFile) && !m_watcher.files().contains(m_stateFile))
			m_watcher.addPath(m_stateFile);
	}

	void setBrightnessValue(int value)
	{
		if (m_brightness == value)
			return;
		m_brightness = value;
		emit brightnessChanged();
	}

	void setReady()
	{
		if (m_ready)
			return;
		m_ready = true;
		emit readyChanged();
	}

	QString m_stateFile;
	int m_brightness = 100;
	int m_maxBrightness = 100;
	bool m_ready = false;
	int m_pendingBrightness = -1;
	QFileSystemWatcher m_watcher;
	QProcess m_query;
	QProcess m_setter;
};

class NotificationServer final : public QObject {
	Q_OBJECT
	Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")

public:
	explicit NotificationServer(QObject *parent = nullptr) : QObject(parent)
	{
	}

	bool registerOnSessionBus()
	{
		QDBusConnection bus = QDBusConnection::sessionBus();
		if (!bus.registerService(QStringLiteral("org.freedesktop.Notifications")))
			return false;
		return bus.registerObject(QStringLiteral("/org/freedesktop/Notifications"),
			this, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
	}

public slots:
	QStringList GetCapabilities() const
	{
		return {QStringLiteral("body"), QStringLiteral("actions"),
			QStringLiteral("icon-static")};
	}

	QString GetServerInformation(QString &vendor, QString &version, QString &specVersion) const
	{
		vendor = QStringLiteral("VXWM");
		version = QStringLiteral("1.0");
		specVersion = QStringLiteral("1.2");
		return QStringLiteral("VXWM Shell");
	}

	uint Notify(const QString &appName, uint replacesId, const QString &appIcon,
	const QString &summary, const QString &body, const QStringList &actions,
		const QVariantMap &hints, int)
	{
		const uint id = replacesId ? replacesId : ++m_lastId;
		QTimer *timer = m_timers.value(id, nullptr);
		if (!timer) {
			timer = new QTimer(this);
			timer->setSingleShot(true);
			m_timers.insert(id, timer);
			connect(timer, &QTimer::timeout, this, [this, id] { closeNotification(id, 1); });
		}
		emit notificationReceived(id, appName,
			resolveIconSource(appIcon, hints.value(QStringLiteral("image-path")).toString()),
			summary, body, actions);
		timer->start(5000);
		return id;
	}

	void CloseNotification(uint id)
	{
		closeNotification(id, 3);
	}

	Q_INVOKABLE void invokeAction(uint id, const QString &actionKey)
	{
		if (!m_timers.contains(id))
			return;
		emit ActionInvoked(id, actionKey);
		closeNotification(id, 2);
	}

signals:
	void notificationReceived(uint id, const QString &appName, const QString &appIcon,
		const QString &summary, const QString &body, const QStringList &actions);
	void NotificationClosed(uint id, uint reason);
	void ActionInvoked(uint id, const QString &actionKey);

private:
	static QString resolveIconSource(const QString &iconName, const QString &imagePath)
	{
		auto localIconUrl = [](const QString &source) {
			const QUrl url(source);
			const QString path = url.isLocalFile() ? url.toLocalFile() : source;
			return QFileInfo::exists(path) && QFileInfo(path).isFile()
				? QUrl::fromLocalFile(path).toString() : QString();
		};

		QString source = localIconUrl(iconName);
		if (!source.isEmpty())
			return source;
		if (iconName.startsWith(QStringLiteral("qrc:/")) ||
			iconName.startsWith(QStringLiteral(":")))
			return iconName;
		source = localIconUrl(imagePath);
		if (!source.isEmpty())
			return source;

		const QString cacheDirectory = QDir::homePath() +
			QStringLiteral("/vxwm/shell/workspace-state/notification-icons");
		if (!QDir().mkpath(cacheDirectory))
			return {};
		for (const QString &name : {iconName, imagePath}) {
			if (name.isEmpty())
				continue;
			const QIcon icon = QIcon::fromTheme(name);
			QString iconFile;
			if (icon.isNull()) {
				static QHash<QString, QString> iconFileCache;
				if (iconFileCache.contains(name)) {
					iconFile = iconFileCache.value(name);
				} else {
					const QStringList filters = {name + QStringLiteral(".svg"),
						name + QStringLiteral(".png"), name + QStringLiteral(".xpm")};
					for (const QString &dataDirectory :
						QStandardPaths::standardLocations(
							QStandardPaths::GenericDataLocation)) {
						for (const QString &subdirectory :
							{QStringLiteral("icons"), QStringLiteral("pixmaps")}) {
							const QString root = dataDirectory + QLatin1Char('/') + subdirectory;
							QDirIterator files(root, filters, QDir::Files,
								QDirIterator::Subdirectories);
							if (files.hasNext()) {
								iconFile = files.next();
								break;
							}
						}
						if (!iconFile.isEmpty())
							break;
					}
					iconFileCache.insert(name, iconFile);
				}
				if (!iconFile.isEmpty())
					return QUrl::fromLocalFile(iconFile).toString();
				continue;
			}
			const QByteArray digest = QCryptographicHash::hash(
				name.toUtf8(), QCryptographicHash::Sha1).toHex();
			const QString path = cacheDirectory + QLatin1Char('/') +
				QString::fromLatin1(digest) + QStringLiteral(".png");
			if (!QFileInfo::exists(path) &&
				!icon.pixmap(QSize(64, 64)).save(path, "PNG"))
				continue;
			return QUrl::fromLocalFile(path).toString();
		}
		return {};
	}

	void closeNotification(uint id, uint reason)
	{
		QTimer *timer = m_timers.take(id);
		if (!timer)
			return;
		timer->stop();
		timer->deleteLater();
		emit NotificationClosed(id, reason);
	}

	QHash<uint, QTimer *> m_timers;
	uint m_lastId = 0;
};

enum AppRole {
	NameRole = Qt::UserRole + 1,
	VersionRole,
	IconRole,
	DesktopIdRole,
	DesktopPathRole,
	CommentRole,
	IconNameRole
};

struct AppEntry {
	QString name;
	QString version;
	QString iconPath;
	QString desktopId;
	QString desktopPath;
	QString iconName;
	QString executablePath;
	QString comment;
};

QString sectionValue(const QString &path, const QString &section)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return {};

	QTextStream stream(&file);
	bool readValue = false;
	while (!stream.atEnd()) {
		const QString line = stream.readLine();
		if (readValue)
			return line;
		readValue = line == section;
	}
	return {};
}

QHash<QString, QString> packageVersions(const QSet<QString> &neededPaths)
{
	QHash<QString, QString> versions;
	QSet<QString> remaining = neededPaths;
	const QDir database(QStringLiteral("/var/lib/pacman/local"));
	if (!database.exists() || remaining.isEmpty())
		return versions;

	const QFileInfoList packages = database.entryInfoList(
		QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
	for (const QFileInfo &package : packages) {
		const QString version = sectionValue(package.filePath() + QStringLiteral("/desc"),
			QStringLiteral("%VERSION%"));
		if (version.isEmpty())
			continue;

		QFile files(package.filePath() + QStringLiteral("/files"));
		if (!files.open(QIODevice::ReadOnly | QIODevice::Text))
			continue;

		QTextStream stream(&files);
		bool inFiles = false;
		while (!stream.atEnd() && !remaining.isEmpty()) {
			const QString line = stream.readLine();
			if (line == QStringLiteral("%FILES%")) {
				inFiles = true;
				continue;
			}
			if (!line.isEmpty() && line.startsWith('%') && line.endsWith('%')) {
				inFiles = false;
				continue;
			}
			if (!inFiles || line.isEmpty())
				continue;

			const QString path = QDir::cleanPath(QStringLiteral("/") + line);
			if (remaining.remove(path))
				versions.insert(path, version);
		}
	}
	return versions;
}

QString appVersion(QSettings &entry)
{
	const QStringList keys = {
		QStringLiteral("X-AppStream-Version"),
		QStringLiteral("X-AppImage-Version"),
		QStringLiteral("X-Flatpak-Version"),
		QStringLiteral("X-KDE-PluginInfo-Version"),
		QStringLiteral("X-GNOME-Version")
	};
	for (const QString &key : keys) {
		const QString version = entry.value(key).toString().trimmed();
		if (!version.isEmpty())
			return version;
	}
	return {};
}

QString appExecutable(const QString &command)
{
	const QStringList arguments = QProcess::splitCommand(command);
	for (const QString &argument : arguments) {
		if (argument == QStringLiteral("env") || argument.startsWith('-')
				|| argument.startsWith('%')
				|| (argument.contains('=') && !argument.contains('/')))
			continue;
		if (QFileInfo(argument).isAbsolute())
			return QDir::cleanPath(argument);
		return QStandardPaths::findExecutable(argument);
	}
	return {};
}

QString displayName(QSettings &entry)
{
	const QString locale = QLocale::system().name();
	const QString language = locale.section('_', 0, 0);
	for (const QString &candidate : {locale, language}) {
		const QString localized = entry.value(QStringLiteral("Name[") + candidate
			+ QLatin1Char(']')).toString().trimmed();
		if (!localized.isEmpty())
			return localized;
	}
	return entry.value(QStringLiteral("Name")).toString().trimmed();
}

QString cacheIcon(const QString &iconName, const QString &cacheDirectory)
{
	if (iconName.isEmpty())
		return {};

	const QByteArray cacheKey = QCryptographicHash::hash(
		(QIcon::themeName() + QLatin1Char('|') + iconName).toUtf8(),
		QCryptographicHash::Sha1).toHex();
	const QString cachePath = cacheDirectory + QLatin1Char('/')
		+ QString::fromLatin1(cacheKey) + QStringLiteral(".png");
	if (QFile::exists(cachePath))
		return QUrl::fromLocalFile(cachePath).toString();

	QIcon icon = QFileInfo(iconName).isAbsolute()
		? QIcon(iconName) : QIcon::fromTheme(iconName);
	if (icon.isNull())
		icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));
	const QPixmap pixmap = icon.pixmap(QSize(40, 40));
	if (pixmap.isNull() || !pixmap.save(cachePath, "PNG"))
		return {};
	return QUrl::fromLocalFile(cachePath).toString();
}

QVector<AppEntry> readApplications(const QString &iconCache)
{
	QDir().mkpath(iconCache);
	const QStringList directories = QStandardPaths::standardLocations(
		QStandardPaths::ApplicationsLocation);
	QSet<QString> seenIds;
	QVector<AppEntry> apps;

	for (const QString &directory : directories) {
		if (!QDir(directory).exists())
			continue;

		QDirIterator files(directory, {QStringLiteral("*.desktop")}, QDir::Files,
			QDirIterator::Subdirectories);
		while (files.hasNext()) {
			const QString filePath = QDir::cleanPath(files.next());
			QString desktopId = QDir(directory).relativeFilePath(filePath);
			desktopId.replace(QLatin1Char('/'), QLatin1Char('-'));
			if (seenIds.contains(desktopId))
				continue;

			QSettings settings(filePath, QSettings::IniFormat);
			settings.beginGroup(QStringLiteral("Desktop Entry"));
			if (settings.value(QStringLiteral("Type")).toString()
					!= QStringLiteral("Application")
				|| settings.value(QStringLiteral("NoDisplay")).toBool()
				|| settings.value(QStringLiteral("Hidden")).toBool())
				continue;

			const QString name = displayName(settings);
			if (name.isEmpty())
				continue;

			const QString absolutePath = QFileInfo(filePath).absoluteFilePath();
			seenIds.insert(desktopId);
			apps.append({name, appVersion(settings), {}, desktopId, absolutePath,
				settings.value(QStringLiteral("Icon")).toString(),
				appExecutable(settings.value(QStringLiteral("Exec")).toString()),
				settings.value(QStringLiteral("Comment")).toString()});
			apps.last().iconPath = cacheIcon(
				settings.value(QStringLiteral("Icon")).toString(), iconCache);
		}
	}

	QSet<QString> needsPackageVersion;
	for (const AppEntry &app : apps) {
		if (app.version.isEmpty()) {
			needsPackageVersion.insert(QDir::cleanPath(app.desktopPath));
			if (!app.executablePath.isEmpty())
				needsPackageVersion.insert(QDir::cleanPath(app.executablePath));
		}
	}
	const QHash<QString, QString> versions = packageVersions(needsPackageVersion);
	for (AppEntry &app : apps) {
		if (app.version.isEmpty()) {
			app.version = versions.value(QDir::cleanPath(app.desktopPath));
			if (app.version.isEmpty())
				app.version = versions.value(QDir::cleanPath(app.executablePath));
			if (app.version.isEmpty())
				app.version = QStringLiteral("Version unavailable");
		}
	}

	std::stable_sort(apps.begin(), apps.end(), [](const AppEntry &a, const AppEntry &b) {
		return QString::localeAwareCompare(a.name, b.name) < 0;
	});
	return apps;
}

class ApplicationListModel final : public QAbstractListModel {
public:
	explicit ApplicationListModel(QVector<AppEntry> apps, QObject *parent = nullptr)
		: QAbstractListModel(parent), m_apps(std::move(apps)) {}

	void setApps(QVector<AppEntry> apps)
	{
		beginResetModel();
		m_apps = std::move(apps);
		endResetModel();
	}

	int rowCount(const QModelIndex &parent = QModelIndex()) const override
	{
		return parent.isValid() ? 0 : m_apps.size();
	}

	QVariant data(const QModelIndex &index, int role) const override
	{
		if (!index.isValid() || index.row() < 0 || index.row() >= m_apps.size())
			return {};
		const AppEntry &app = m_apps.at(index.row());
		switch (role) {
		case Qt::DisplayRole: return app.name;
		case NameRole: return app.name;
		case VersionRole: return app.version;
		case IconRole: return app.iconPath;
		case DesktopIdRole: return app.desktopId;
		case DesktopPathRole: return app.desktopPath;
		case CommentRole: return app.comment;
		case IconNameRole: return app.iconName;
		default: return {};
		}
	}

	QHash<int, QByteArray> roleNames() const override
	{
		return {
			{NameRole, "appName"},
			{VersionRole, "appVersion"},
			{IconRole, "iconPath"},
			{DesktopIdRole, "desktopId"},
			{DesktopPathRole, "desktopPath"},
			{CommentRole, "comment"},
			{IconNameRole, "iconName"}
		};
	}

private:
	QVector<AppEntry> m_apps;
};

} // namespace

class ApplicationCatalog final : public QSortFilterProxyModel {
	Q_OBJECT
	Q_PROPERTY(int count READ count NOTIFY countChanged)
	Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)
	Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
	Q_PROPERTY(bool clipboardMode READ clipboardMode WRITE setClipboardMode NOTIFY clipboardModeChanged)
	Q_PROPERTY(int clipboardRevision READ clipboardRevision NOTIFY clipboardRevisionChanged)

public:
	explicit ApplicationCatalog(const QString &iconCache, QObject *parent = nullptr)
		: QSortFilterProxyModel(parent)
		, m_iconCache(iconCache)
	{
		m_appsModel = new ApplicationListModel(readApplications(iconCache), this);
		m_clipboardModel = new ApplicationListModel({}, this);
		m_stateDirectory = QFileInfo(iconCache).absolutePath();
		m_clipboardDirectory = m_stateDirectory
			+ QStringLiteral("/clipboard-history");
		setSourceModel(m_appsModel);
		setDynamicSortFilter(true);
		setSortRole(Qt::DisplayRole);
		sort(0, Qt::AscendingOrder);
		connect(QGuiApplication::clipboard(), &QClipboard::dataChanged,
		this, [this] {
			captureClipboard();
			if (m_clipboardMode)
				refreshClipboard();
		});
		captureClipboard();
	}

	int count() const { return rowCount(); }
	int totalCount() const
	{
		const auto *model = m_clipboardMode ? m_clipboardModel : m_appsModel;
		return model ? model->rowCount() : 0;
	}
	QString filterText() const { return m_filterText; }
	bool clipboardMode() const { return m_clipboardMode; }
	int clipboardRevision() const { return m_clipboardRevision; }

	Q_INVOKABLE int entryHeight(int row) const
	{
		if (row < 0 || row >= count())
			return 0;
		if (!m_clipboardMode)
			return 56;
		const QModelIndex source = mapToSource(index(row, 0));
		if (!source.data(IconRole).toString().isEmpty())
			return 116;

		QFont font(QStringLiteral("Google Sans Flex"));
		font.setPointSize(10);
		QTextLayout layout(source.data(Qt::DisplayRole).toString(), font);
		QTextOption option;
		option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
		layout.setTextOption(option);
		layout.beginLayout();
		int lineCount = 0;
		while (lineCount < 4) {
			QTextLine line = layout.createLine();
			if (!line.isValid())
				break;
			line.setLineWidth(346);
			++lineCount;
		}
		layout.endLayout();
		return qMax(56, lineCount * QFontMetrics(font).height() + 16);
	}

	Q_INVOKABLE QString entryName(int row) const
	{
		if (row < 0 || row >= count())
			return {};
		const QString name = mapToSource(index(row, 0)).data(Qt::DisplayRole).toString().trimmed();
		return name.isEmpty() ? QStringLiteral("Unknown application") : name;
	}

	Q_INVOKABLE QString entryVersion(int row) const
	{
		if (row < 0 || row >= count())
			return {};
		const QString version = mapToSource(index(row, 0)).data(VersionRole).toString().trimmed();
		return version.isEmpty() ? QStringLiteral("Version unavailable") : version;
	}

	Q_INVOKABLE QString entryIconPath(int row) const
	{
		if (row < 0 || row >= count())
			return {};
		return mapToSource(index(row, 0)).data(IconRole).toString();
	}

	Q_INVOKABLE void consumeClipboardLaunchRequests()
	{
		QDir directory(m_stateDirectory);
		for (const QString &name : directory.entryList(
				{QStringLiteral("launcher-clipboard-request-*")}, QDir::Files))
			directory.remove(name);
	}

	void setClipboardMode(bool enabled)
	{
		if (enabled == m_clipboardMode)
			return;
		if (enabled)
			captureClipboard();
		m_clipboardMode = enabled;
		setSourceModel(enabled ? static_cast<QAbstractItemModel *>(m_clipboardModel)
			: static_cast<QAbstractItemModel *>(m_appsModel));
		if (enabled)
			sort(-1);
		else
			sort(0, Qt::AscendingOrder);
		if (enabled)
			refreshClipboard();
		emit clipboardModeChanged();
		emit countChanged();
	}

	void setFilterText(const QString &text)
	{
		const QString normalized = text.trimmed();
		if (m_filterText == normalized)
			return;
		beginFilterChange();
		m_filterText = normalized;
		endFilterChange(Direction::Rows);
		emit filterTextChanged();
		emit countChanged();
	}

	Q_INVOKABLE bool launch(int row)
	{
		if (row < 0 || row >= count())
			return false;
		const QModelIndex sourceIndex = mapToSource(index(row, 0));
		const QString path = sourceIndex.data(DesktopPathRole).toString();
		if (path.isEmpty() || !QProcess::startDetached(
				QStringLiteral("gio"), {QStringLiteral("launch"), path})) {
			emit launchFailed(QStringLiteral("Could not launch the selected application."));
			return false;
		}
		emit applicationLaunched();
		return true;
	}

	Q_INVOKABLE bool copyClipboard(int row)
	{
		if (row < 0 || row >= count() || !m_clipboardMode)
			return false;
		const QString id = mapToSource(index(row, 0)).data(DesktopIdRole).toString();
		const QString path = mapToSource(index(row, 0)).data(DesktopPathRole).toString();
		QFile file(path);
		if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
			emit launchFailed(QStringLiteral("Could not read that clipboard item."));
			return false;
		}
		const QByteArray data = file.readAll();
		auto *mimeData = new QMimeData;
		const QImage image = id.endsWith(QStringLiteral(".png"))
			? QImage::fromData(data) : QImage();
		if (!image.isNull())
			mimeData->setImageData(image);
		else
			mimeData->setText(QString::fromUtf8(data));
		QGuiApplication::clipboard()->setMimeData(mimeData);
		emit applicationLaunched();
		return true;
	}

signals:
	void countChanged();
	void filterTextChanged();
	void applicationLaunched();
	void clipboardModeChanged();
	void clipboardRevisionChanged();
	void launchFailed(const QString &message);

protected:
	bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override
	{
		if (m_filterText.isEmpty())
			return true;
		const QModelIndex item = sourceModel()->index(sourceRow, 0, sourceParent);
		for (const int role : {NameRole, CommentRole, DesktopIdRole}) {
			if (item.data(role).toString().contains(m_filterText, Qt::CaseInsensitive))
				return true;
		}
		return false;
	}

private:
	void captureClipboard()
	{
		const QMimeData *mimeData = QGuiApplication::clipboard()->mimeData();
		QByteArray data;
		QString suffix;
		if (mimeData->hasImage()) {
			const QImage image = qvariant_cast<QImage>(mimeData->imageData());
			QBuffer buffer(&data);
			if (image.isNull() || !buffer.open(QIODevice::WriteOnly)
					|| !image.save(&buffer, "PNG"))
				return;
			suffix = QStringLiteral("png");
		} else if (mimeData->hasText()) {
			data = mimeData->text().toUtf8();
			if (data.isEmpty())
				return;
			suffix = QStringLiteral("txt");
		} else {
			return;
		}

		QDir directory(m_clipboardDirectory);
		if (!directory.mkpath(QStringLiteral(".")))
			return;
		const QString digest = QString::fromLatin1(
			QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
		for (const QString &old : directory.entryList(
				{QStringLiteral("*-") + digest + QStringLiteral(".*")}, QDir::Files))
			directory.remove(old);

		const QString name = QString::number(QDateTime::currentMSecsSinceEpoch())
			+ QLatin1Char('-') + digest + QLatin1Char('.') + suffix;
		QSaveFile file(directory.filePath(name));
		if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()
				|| !file.commit())
			return;

	}

	void refreshClipboard()
	{
		QVector<AppEntry> entries;
		QDir directory(m_clipboardDirectory);
		const QStringList files = directory.entryList(QDir::Files,
			QDir::Name | QDir::Reversed);
		for (const QString &name : files) {
			const QString path = directory.filePath(name);
			const bool isImage = name.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive);
			QString preview = QStringLiteral("Image");
			if (!isImage) {
				QFile file(path);
				if (!file.open(QIODevice::ReadOnly))
					continue;
				preview = QString::fromUtf8(file.read(513)).trimmed();
			}
			if (!isImage && preview.size() > 512)
				preview = preview.left(512).trimmed() + QChar(0x2026);
			if (preview.isEmpty())
				continue;
			const QString iconPath = isImage ? QUrl::fromLocalFile(path).toString() : QString();
			entries.append({preview, isImage ? QStringLiteral("Image") : QStringLiteral("Text"),
				iconPath, QStringLiteral("x11:") + name, path, {}, preview});
		}
		m_clipboardModel->setApps(std::move(entries));
		++m_clipboardRevision;
		emit clipboardRevisionChanged();
		emit countChanged();
	}

	QString m_filterText;
	bool m_clipboardMode = false;
	ApplicationListModel *m_appsModel = nullptr;
	ApplicationListModel *m_clipboardModel = nullptr;
	QString m_iconCache;
	QString m_stateDirectory;
	QString m_clipboardDirectory;
	int m_clipboardRevision = 0;
};

class TrayController final : public QObject {
	Q_OBJECT

public:
	Q_INVOKABLE void invokeTrayItem(int id, bool contextMenu, int x, int y)
	{
		if (id <= 0)
			return;
		QDBusMessage call = QDBusMessage::createMethodCall(
			QStringLiteral("org.vxwm.TrayHost"), QStringLiteral("/TrayHost"),
			QStringLiteral("org.vxwm.TrayHost"), QStringLiteral("InvokeItem"));
		call << id << contextMenu << x << y;
		QDBusConnection::sessionBus().call(call, QDBus::NoBlock);
	}
};

class WindowSwitcherController final : public QObject {
	Q_OBJECT

public:
	explicit WindowSwitcherController(const QString &stateDirectory,
		QObject *parent = nullptr)
		: QObject(parent), m_stateDirectory(stateDirectory)
	{
	}

	Q_INVOKABLE QString iconPath(const QString &iconName)
	{
		const QString safeName = QString(iconName).replace(
			QRegularExpression(QStringLiteral("[^A-Za-z0-9._+-]")),
			QStringLiteral("_"));
		if (safeName.isEmpty())
			return {};
		const QString directory = m_stateDirectory + QStringLiteral("/window-icons");
		QDir().mkpath(directory);
		const QString path = directory + QLatin1Char('/') + safeName
			+ QStringLiteral(".png");
		if (!QFileInfo::exists(path)) {
			QIcon icon = QIcon::fromTheme(iconName);
			if (icon.isNull())
				icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));
			if (icon.isNull() || !icon.pixmap(64, 64).save(path, "PNG"))
				return {};
		}
		return QUrl::fromLocalFile(path).toString();
	}

	Q_INVOKABLE bool activateWindow(const QString &windowId)
	{
		bool ok = false;
		const Window target = windowId.toULongLong(&ok, 16);
		if (!ok || target == None)
			return false;
		Display *display = XOpenDisplay(nullptr);
		if (!display)
			return false;
		XEvent event{};
		event.xclient.type = ClientMessage;
		event.xclient.display = display;
		event.xclient.window = target;
		event.xclient.message_type = XInternAtom(display,
			"_VXWM_ACTIVATE_WINDOW", False);
		event.xclient.format = 32;
		const Status sent = XSendEvent(display, DefaultRootWindow(display), False,
			SubstructureRedirectMask, &event);
		XFlush(display);
		XCloseDisplay(display);
		return sent != 0;
	}

	Q_INVOKABLE QString windowTitle(const QString &windowId) const
	{
		bool ok = false;
		const Window target = windowId.toULongLong(&ok, 16);
		if (!ok || target == None)
			return {};
		Display *display = XOpenDisplay(nullptr);
		if (!display)
			return {};
		const Atom nameAtom = XInternAtom(display, "_NET_WM_NAME", False);
		const Atom utf8Atom = XInternAtom(display, "UTF8_STRING", False);
		Atom actualType = None;
		int actualFormat = 0;
		unsigned long itemCount = 0;
		unsigned long bytesAfter = 0;
		unsigned char *data = nullptr;
		QString title;
		if (XGetWindowProperty(display, target, nameAtom, 0, 1024, False,
			utf8Atom, &actualType, &actualFormat, &itemCount, &bytesAfter,
			&data) == Success && actualType == utf8Atom && actualFormat == 8 && data) {
			title = QString::fromUtf8(reinterpret_cast<const char *>(data),
				static_cast<qsizetype>(itemCount));
		}
		if (data)
			XFree(data);
		if (title.isEmpty()) {
			char *legacyTitle = nullptr;
			if (XFetchName(display, target, &legacyTitle) && legacyTitle) {
				title = QString::fromLocal8Bit(legacyTitle);
				XFree(legacyTitle);
			}
		}
		XCloseDisplay(display);
		return title.simplified();
	}

	Q_INVOKABLE void closeSwitcher()
	{
		removeRequests(QStringLiteral("window-switcher-open"));
		removeRequests(QStringLiteral("window-switcher-commit"));
		consumeNextRequests();
	}

	Q_INVOKABLE void consumeNextRequests()
	{
		const QStringList requests = QDir(m_stateDirectory).entryList(
			{QStringLiteral("window-switcher-next-*")}, QDir::Files);
		for (const QString &request : requests)
			removeRequests(request);
	}

private:
	void removeRequests(const QString &name)
	{
		QFile::remove(m_stateDirectory + QLatin1Char('/') + name);
	}

	QString m_stateDirectory;
};

class ProcessRamMonitor final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QVariantList topProcesses READ topProcesses NOTIFY topProcessesChanged)
	Q_PROPERTY(QVariantList cpuProcesses READ cpuProcesses NOTIFY cpuProcessesChanged)
	Q_PROPERTY(QVariantList gpuProcesses READ gpuProcesses NOTIFY gpuProcessesChanged)
	Q_PROPERTY(QString memorySummary READ memorySummary NOTIFY memorySummaryChanged)
	Q_PROPERTY(int cpuUsage READ cpuUsage NOTIFY usageChanged)
	Q_PROPERTY(int gpuUsage READ gpuUsage NOTIFY usageChanged)

public:
	explicit ProcessRamMonitor(const QString &stateDirectory, QObject *parent = nullptr)
		: QObject(parent), m_stateDirectory(stateDirectory)
	{
		connect(&m_timer, &QTimer::timeout, this, &ProcessRamMonitor::refresh);
		connect(&m_gpuTimer, &QTimer::timeout, this, &ProcessRamMonitor::sampleGpuProcesses);
		connect(&m_gpuSampler,
			qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
			this, &ProcessRamMonitor::updateGpuProcesses);
		m_timer.start(3000);
		m_gpuTimer.start(6000);
		sampleGpuProcesses();
		refresh();
	}

	QVariantList topProcesses() const { return m_topProcesses; }
	QVariantList cpuProcesses() const { return m_cpuProcesses; }
	QVariantList gpuProcesses() const { return m_gpuProcesses; }
	QString memorySummary() const { return m_memorySummary; }
	int cpuUsage() const { return m_cpuUsage; }
	int gpuUsage() const { return m_gpuUsage; }

signals:
	void topProcessesChanged();
	void cpuProcessesChanged();
	void gpuProcessesChanged();
	void memorySummaryChanged();
	void usageChanged();

private slots:
	void refresh()
	{
		const int cpuUsage = readUsage(QStringLiteral("cpu-"));
		const int gpuUsage = readUsage(QStringLiteral("gpu-"));
		if (cpuUsage != m_cpuUsage || gpuUsage != m_gpuUsage) {
			m_cpuUsage = cpuUsage;
			m_gpuUsage = gpuUsage;
			emit usageChanged();
		}

		quint64 totalKilobytes = 0;
		quint64 availableKilobytes = 0;
		quint64 systemUsedKilobytes = 0;
		const long systemPageSize = sysconf(_SC_PAGESIZE);
		const long physicalPages = sysconf(_SC_PHYS_PAGES);
		const long availablePages = sysconf(_SC_AVPHYS_PAGES);
		const quint64 pageSizeKiB = systemPageSize > 0
			? static_cast<quint64>(systemPageSize / 1024) : 4;
		if (physicalPages > 0 && availablePages >= 0) {
			totalKilobytes = static_cast<quint64>(physicalPages) * pageSizeKiB;
			availableKilobytes = static_cast<quint64>(availablePages) * pageSizeKiB;
		}
		QFile meminfo(QStringLiteral("/proc/meminfo"));
		if (meminfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
			const QString memoryInfo = QString::fromLatin1(meminfo.readAll());
			const auto totalMatch = QRegularExpression(
				QStringLiteral("^MemTotal:\\s*(\\d+)"),
				QRegularExpression::MultilineOption).match(memoryInfo);
			const auto availableMatch = QRegularExpression(
				QStringLiteral("^MemAvailable:\\s*(\\d+)"),
				QRegularExpression::MultilineOption).match(memoryInfo);
			bool totalOk = false;
			bool availableOk = false;
			const quint64 parsedTotal = totalMatch.captured(1).toULongLong(&totalOk);
			const quint64 parsedAvailable = availableMatch.captured(1).toULongLong(&availableOk);
			if (totalOk && availableOk) {
				totalKilobytes = parsedTotal;
				availableKilobytes = parsedAvailable;
			}
		}
		if (totalKilobytes > 0 && availableKilobytes <= totalKilobytes) {
			systemUsedKilobytes = totalKilobytes - availableKilobytes;
			const double usedGigabytes = static_cast<double>(totalKilobytes - availableKilobytes)
				/ (1024.0 * 1024.0);
			const double totalGigabytes = static_cast<double>(totalKilobytes)
				/ (1024.0 * 1024.0);
			const QString summary = QString::number(usedGigabytes, 'f', 1)
				+ QStringLiteral(" / ") + QString::number(totalGigabytes, 'f', 1)
				+ QStringLiteral(" GB");
			if (summary != m_memorySummary) {
				m_memorySummary = summary;
				emit memorySummaryChanged();
			}
		}

		QHash<QString, quint64> groupedPssKilobytes;
		QHash<QString, quint64> groupedFallbackKilobytes;
		QHash<QString, quint64> processTicks;
		QHash<QString, QString> processNames;
		const QDir procDirectory(QStringLiteral("/proc"));
		const double elapsedSeconds = m_cpuSampleClock.isValid()
			? static_cast<double>(m_cpuSampleClock.elapsed()) / 1000.0 : 0.0;
		m_cpuSampleClock.start();
		for (const QString &pid : procDirectory.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
			bool isPid = false;
			pid.toLongLong(&isPid);
			if (!isPid)
				continue;

			const QString processDirectory = procDirectory.filePath(pid);
			QString executable = QFileInfo(processDirectory + QStringLiteral("/exe")).symLinkTarget();
			QString name = QFileInfo(executable).fileName();
			if (name.isEmpty()) {
				QFile comm(processDirectory + QStringLiteral("/comm"));
				if (!comm.open(QIODevice::ReadOnly))
					continue;
				name = QString::fromLocal8Bit(comm.readAll()).trimmed();
			}
			if (name.isEmpty())
				continue;
			QFile processStat(processDirectory + QStringLiteral("/stat"));
			if (processStat.open(QIODevice::ReadOnly)) {
				const QByteArray statLine = processStat.readLine();
				const qsizetype commEnd = statLine.lastIndexOf(')');
				if (commEnd >= 0) {
					const QList<QByteArray> fields = statLine.mid(commEnd + 2).simplified().split(' ');
					if (fields.size() > 12) {
						bool userOk = false;
						bool systemOk = false;
						const quint64 userTicks = fields.at(11).toULongLong(&userOk);
						const quint64 systemTicks = fields.at(12).toULongLong(&systemOk);
						if (userOk && systemOk) {
							processTicks.insert(pid, userTicks + systemTicks);
						processNames.insert(pid, name);
						}
					}
				}
			}
			const QString group = name.toCaseFolded();
			QFile statm(processDirectory + QStringLiteral("/statm"));
			if (!statm.open(QIODevice::ReadOnly))
				continue;
			const QList<QByteArray> statmFields = statm.readAll().simplified().split(' ');
			if (statmFields.size() < 2)
				continue;
			bool rssOk = false;
			const quint64 residentPages = statmFields.at(1).toULongLong(&rssOk);
			if (!rssOk)
				continue;
			const quint64 residentKilobytes = residentPages * pageSizeKiB;

			QFile smaps(processDirectory + QStringLiteral("/smaps_rollup"));
			quint64 proportionalKilobytes = 0;
			bool foundPss = false;
			if (smaps.open(QIODevice::ReadOnly | QIODevice::Text)) {
				while (!smaps.atEnd()) {
					const QByteArray line = smaps.readLine();
					if (!line.startsWith("Pss:"))
						continue;
					const QList<QByteArray> fields = line.simplified().split(' ');
					if (fields.size() >= 2) {
						bool ok = false;
						proportionalKilobytes = fields.at(1).toULongLong(&ok);
						foundPss = ok;
					}
					break;
				}
			}
			if (foundPss)
				groupedPssKilobytes[group] += proportionalKilobytes;
			else
				groupedFallbackKilobytes[group] += residentKilobytes;
		}
		if (elapsedSeconds > 0.0) {
			QHash<QString, double> cpuByName;
			QHash<QString, QString> cpuDisplayNames;
			const double ticksPerSecond = static_cast<double>(sysconf(_SC_CLK_TCK));
			const double processorCount = static_cast<double>(
				std::max<long>(1, sysconf(_SC_NPROCESSORS_ONLN)));
			for (auto it = processTicks.cbegin(); it != processTicks.cend(); ++it) {
				auto previous = m_previousProcessTicks.constFind(it.key());
				if (previous == m_previousProcessTicks.cend() || it.value() < previous.value())
					continue;
				const QString name = processNames.value(it.key());
				const QString group = name.toCaseFolded();
				cpuByName[group] += (static_cast<double>(it.value() - previous.value())
					/ ticksPerSecond / elapsedSeconds / processorCount) * 100.0;
				cpuDisplayNames.insert(group, name);
			}
			QVector<QPair<QString, double>> sortedCpu;
			for (auto it = cpuByName.cbegin(); it != cpuByName.cend(); ++it)
				sortedCpu.append({it.key(), it.value()});
			std::sort(sortedCpu.begin(), sortedCpu.end(), [](const auto &left, const auto &right) {
				return left.second > right.second;
			});
			QVariantList nextCpu;
			for (qsizetype i = 0; i < std::min<qsizetype>(3, sortedCpu.size()); ++i) {
				const auto &[key, usage] = sortedCpu.at(i);
				QString displayName = cpuDisplayNames.value(key);
				displayName[0] = displayName.at(0).toUpper();
				nextCpu.append(QVariantMap{
					{QStringLiteral("name"), displayName},
					{QStringLiteral("usage"), QString::number(usage, 'f', 1) + QStringLiteral("%")}
				});
			}
			if (nextCpu != m_cpuProcesses) {
				m_cpuProcesses = std::move(nextCpu);
				emit cpuProcessesChanged();
			}
		}
		m_previousProcessTicks = std::move(processTicks);

		QSet<QString> groupNames;
		for (auto it = groupedPssKilobytes.cbegin(); it != groupedPssKilobytes.cend(); ++it)
			groupNames.insert(it.key());
		for (auto it = groupedFallbackKilobytes.cbegin(); it != groupedFallbackKilobytes.cend(); ++it)
			groupNames.insert(it.key());
		QHash<QString, quint64> groupedKilobytes;
		quint64 groupedTotalKilobytes = 0;
		for (const QString &name : groupNames) {
			const quint64 value = groupedPssKilobytes.value(name)
				+ groupedFallbackKilobytes.value(name);
			groupedKilobytes.insert(name, value);
			groupedTotalKilobytes += value;
		}
		if (systemUsedKilobytes > 0 && groupedTotalKilobytes > systemUsedKilobytes) {
			const double scale = static_cast<double>(systemUsedKilobytes)
				/ static_cast<double>(groupedTotalKilobytes);
			for (auto it = groupedKilobytes.begin(); it != groupedKilobytes.end(); ++it)
				it.value() = static_cast<quint64>(static_cast<double>(it.value()) * scale);
		}

		QVector<QPair<QString, quint64>> sorted;
		sorted.reserve(groupedKilobytes.size());
		for (auto it = groupedKilobytes.cbegin(); it != groupedKilobytes.cend(); ++it)
			sorted.append({it.key(), it.value()});
		std::sort(sorted.begin(), sorted.end(), [](const auto &left, const auto &right) {
			return left.second > right.second;
		});

		QVariantList next;
		for (qsizetype i = 0; i < std::min<qsizetype>(6, sorted.size()); ++i) {
			const auto &[key, kibibytes] = sorted.at(i);
			QString displayName = key;
			displayName[0] = displayName.at(0).toUpper();
			const double gigabytes = static_cast<double>(kibibytes) / (1024.0 * 1024.0);
			next.append(QVariantMap{
				{QStringLiteral("name"), displayName},
				{QStringLiteral("memory"), QString::number(gigabytes, 'f', 2) + QStringLiteral(" GB")}
			});
		}
		if (next == m_topProcesses)
			return;
		m_topProcesses = std::move(next);
		emit topProcessesChanged();
	}

	void sampleGpuProcesses()
	{
		if (m_gpuSampler.state() != QProcess::NotRunning)
			return;
		QString executable = QStandardPaths::findExecutable(QStringLiteral("nvidia-smi"));
		if (executable.isEmpty())
			executable = QStringLiteral("/usr/sbin/nvidia-smi");
		m_gpuSampler.start(executable,
			{QStringLiteral("pmon"), QStringLiteral("-c"), QStringLiteral("1"),
			 QStringLiteral("-s"), QStringLiteral("um")});
	}

	void updateGpuProcesses(int exitCode, QProcess::ExitStatus status)
	{
		QVariantList nextGpu;
		if (exitCode == 0 && status == QProcess::NormalExit) {
			QHash<QString, double> usageByApp;
			QHash<QString, double> memoryByApp;
			QHash<QString, QString> nameByApp;
			const QStringList lines = QString::fromLocal8Bit(
				m_gpuSampler.readAllStandardOutput()).split(QLatin1Char('\n'));
			for (const QString &line : lines) {
				const QStringList fields = line.simplified().split(QLatin1Char(' '));
				if (fields.size() < 5 || fields.first().startsWith(QLatin1Char('#')))
					continue;
				bool pidOk = false;
				const qint64 pid = fields.at(1).toLongLong(&pidOk);
				bool usageOk = false;
				double usage = fields.at(3).toDouble(&usageOk);
				bool memoryOk = false;
				const double gpuMemory = fields.size() > 9
					? fields.at(9).toDouble(&memoryOk) : 0.0;
				if (!pidOk || ((!usageOk || usage < 0.0) && (!memoryOk || gpuMemory <= 0.0)))
					continue;
				if (!usageOk)
					usage = 0.0;
				QString name = QFileInfo(QStringLiteral("/proc/%1/exe").arg(pid)).symLinkTarget();
				name = QFileInfo(name).fileName();
				if (name.isEmpty()) {
					QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
					if (comm.open(QIODevice::ReadOnly))
						name = QString::fromLocal8Bit(comm.readAll()).trimmed();
				}
				if (name.isEmpty())
					name = fields.last();
				name.remove(QStringLiteral(" (deleted)"));
				const QString group = name.toCaseFolded();
				usageByApp[group] += usage;
				if (memoryOk)
					memoryByApp[group] += gpuMemory;
				nameByApp.insert(group, name);
			}

			QVector<QPair<QString, double>> sortedGpu;
			for (auto it = usageByApp.cbegin(); it != usageByApp.cend(); ++it)
				sortedGpu.append({it.key(), it.value()});
			std::sort(sortedGpu.begin(), sortedGpu.end(), [&memoryByApp](const auto &left, const auto &right) {
				if (left.second != right.second)
					return left.second > right.second;
				return memoryByApp.value(left.first) > memoryByApp.value(right.first);
			});
			for (qsizetype i = 0; i < std::min<qsizetype>(3, sortedGpu.size()); ++i) {
				const auto &[key, usage] = sortedGpu.at(i);
				QString displayName = nameByApp.value(key);
				displayName[0] = displayName.at(0).toUpper();
				nextGpu.append(QVariantMap{
					{QStringLiteral("name"), displayName},
					{QStringLiteral("usage"), QString::number(usage, 'f', 1) + QStringLiteral("%")}
				});
			}
		}
		if (nextGpu != m_gpuProcesses) {
			m_gpuProcesses = std::move(nextGpu);
			emit gpuProcessesChanged();
		}
	}

private:
	int readUsage(const QString &prefix) const
	{
		const QStringList markers = QDir(m_stateDirectory).entryList(
			QStringList{prefix + QLatin1Char('*')}, QDir::Files, QDir::Name);
		if (markers.isEmpty())
			return 0;
		bool ok = false;
		const int value = markers.constLast().mid(prefix.size()).toInt(&ok);
		return ok ? std::clamp(value, 0, 100) : 0;
	}

	QTimer m_timer;
	QTimer m_gpuTimer;
	QProcess m_gpuSampler;
	QElapsedTimer m_cpuSampleClock;
	QString m_stateDirectory;
	QVariantList m_topProcesses;
	QVariantList m_cpuProcesses;
	QVariantList m_gpuProcesses;
	QHash<QString, quint64> m_previousProcessTicks;
	QString m_memorySummary = QStringLiteral("-- / -- GB");
	int m_cpuUsage = 0;
	int m_gpuUsage = 0;
};

int main(int argc, char **argv)
{
	QGuiApplication application(argc, argv);
	const QRect virtualScreenGeometry = QGuiApplication::primaryScreen()
		? QGuiApplication::primaryScreen()->virtualGeometry() : QRect(0, 0, 1, 1);
	const QString vxwmRoot = QDir::homePath() + QStringLiteral("/vxwm");
	const QString iconCache = vxwmRoot
		+ QStringLiteral("/shell/workspace-state/launcher-icons");
	const QString stateDirectory = vxwmRoot
		+ QStringLiteral("/shell/workspace-state");
	const struct passwd *sessionAccount = getpwuid(getuid());
	const QString sessionUsername = sessionAccount
		? QString::fromLocal8Bit(sessionAccount->pw_name)
		: qEnvironmentVariable("USER");
	ApplicationCatalog catalog(iconCache);
	KeyboardLayoutState keyboardLayout(stateDirectory);
	PowerMenuState powerMenu(stateDirectory);
	LockScreenState lockScreen(stateDirectory,
		vxwmRoot + QStringLiteral("/shell/password-check"), sessionUsername);
	AudioVolume audioVolume(stateDirectory);
	BrightnessControl brightnessControl(stateDirectory);
	NotificationServer notifications;
	TrayController trayController;
	WindowSwitcherController windowSwitcher(stateDirectory);
	ProcessRamMonitor processRamMonitor(stateDirectory);
	if (!notifications.registerOnSessionBus())
		qWarning() << "Could not register VXWM notification service:" << QDBusConnection::sessionBus().lastError().message();
	QQmlApplicationEngine engine;
	engine.rootContext()->setContextProperty(QStringLiteral("virtualScreenGeometry"),
		virtualScreenGeometry);
	engine.rootContext()->setContextProperty(QStringLiteral("appCatalog"), &catalog);
	engine.rootContext()->setContextProperty(QStringLiteral("systemKeyboardLayout"),
		&keyboardLayout);
	engine.rootContext()->setContextProperty(QStringLiteral("powerMenuState"),
		&powerMenu);
	engine.rootContext()->setContextProperty(QStringLiteral("lockScreenState"),
		&lockScreen);
	engine.rootContext()->setContextProperty(QStringLiteral("audioVolume"), &audioVolume);
	engine.rootContext()->setContextProperty(QStringLiteral("brightnessControl"),
		&brightnessControl);
	engine.rootContext()->setContextProperty(QStringLiteral("notificationServer"),
		&notifications);
	engine.rootContext()->setContextProperty(QStringLiteral("trayControl"),
		&trayController);
	engine.rootContext()->setContextProperty(QStringLiteral("windowSwitcher"),
		&windowSwitcher);
	engine.rootContext()->setContextProperty(QStringLiteral("processRamMonitor"),
		&processRamMonitor);
	QObject::connect(&engine, &QQmlApplicationEngine::warnings,
		[](const QList<QQmlError> &warnings) {
		for (const QQmlError &warning : warnings)
			qWarning().noquote() << warning.toString();
	});
	QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
		[](QObject *object, const QUrl &url) {
		if (object == nullptr)
			qWarning() << "VXWM QML root failed to load:" << url;
	});

	const QString qmlPath = argc > 1
		? QString::fromLocal8Bit(argv[1])
		: vxwmRoot + QStringLiteral("/shell/main.qml");
	engine.load(QUrl::fromLocalFile(qmlPath));
	if (engine.rootObjects().isEmpty())
		return 1;
	return application.exec();
}

#include "main.moc"
