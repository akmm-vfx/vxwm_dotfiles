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
	IconNameRole,
	ExecutablePathRole
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
	if (!QDir().mkpath(cacheDirectory))
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

	void setIconPath(int row, const QString &path)
	{
		if (row < 0 || row >= m_apps.size() || m_apps[row].iconPath == path)
			return;
		m_apps[row].iconPath = path;
		const QModelIndex changed = index(row, 0);
		emit dataChanged(changed, changed, {IconRole});
	}

	void setVersion(int row, const QString &version)
	{
		if (row < 0 || row >= m_apps.size() || m_apps[row].version == version)
			return;
		m_apps[row].version = version;
		const QModelIndex changed = index(row, 0);
		emit dataChanged(changed, changed, {VersionRole});
	}

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
		case ExecutablePathRole: return app.executablePath;
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
			{IconNameRole, "iconName"},
			{ExecutablePathRole, "executablePath"}
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
		return version.isEmpty() ? QStringLiteral("Checking…") : version;
	}

	Q_INVOKABLE QString entryIconPath(int row)
	{
		if (row < 0 || row >= count())
			return {};
		const QModelIndex source = mapToSource(index(row, 0));
		QString path = source.data(IconRole).toString();
		if (!path.isEmpty() || m_clipboardMode)
			return path;
		path = cacheIcon(source.data(IconNameRole).toString(), m_iconCache);
		if (!path.isEmpty())
			m_appsModel->setIconPath(source.row(), path);
		return path;
	}

	Q_INVOKABLE void resolveVersions(int firstRow, int rowCount)
	{
		if (m_clipboardMode || rowCount <= 0)
			return;
		QSet<QString> neededPaths;
		const int lastRow = std::min(count(), firstRow + rowCount);
		for (int row = std::max(0, firstRow); row < lastRow; ++row) {
			const QModelIndex source = mapToSource(index(row, 0));
			if (!source.data(VersionRole).toString().isEmpty())
				continue;
			for (const int role : {DesktopPathRole, ExecutablePathRole}) {
				const QString path = source.data(role).toString();
				if (!path.isEmpty())
					neededPaths.insert(path);
			}
		}
		if (neededPaths.isEmpty())
			return;

		const QHash<QString, QString> versions = packageVersions(neededPaths);
		for (int row = std::max(0, firstRow); row < lastRow; ++row) {
			const QModelIndex source = mapToSource(index(row, 0));
			if (!source.data(VersionRole).toString().isEmpty())
				continue;
			QString version = versions.value(source.data(DesktopPathRole).toString());
			if (version.isEmpty())
				version = versions.value(source.data(ExecutablePathRole).toString());
			m_appsModel->setVersion(source.row(), version.isEmpty()
				? QStringLiteral("Version unavailable") : version);
		}
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
	Q_PROPERTY(QString memorySummary READ memorySummary NOTIFY memorySummaryChanged)

public:
	explicit ProcessRamMonitor(QObject *parent = nullptr)
		: QObject(parent)
	{
		connect(&m_memoryTimer, &QTimer::timeout, this, &ProcessRamMonitor::refreshMemorySummary);
		connect(&m_processTimer, &QTimer::timeout, this, &ProcessRamMonitor::refreshTopProcesses);
		m_memoryTimer.start(3000);
		m_processTimer.start(15000);
		refreshMemorySummary();
		refreshTopProcesses();
	}

	QVariantList topProcesses() const { return m_topProcesses; }
	QString memorySummary() const { return m_memorySummary; }

signals:
	void topProcessesChanged();
	void memorySummaryChanged();

private slots:
	void refreshMemorySummary()
	{
		quint64 totalKilobytes = 0;
		quint64 availableKilobytes = 0;
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
	}

	void refreshTopProcesses()
	{
		struct ProcessSample {
			QString group;
			QString directory;
			quint64 residentKilobytes;
		};
		QVector<ProcessSample> samples;
		QHash<QString, quint64> residentByGroup;
		const long systemPageSize = sysconf(_SC_PAGESIZE);
		const quint64 pageSizeKiB = systemPageSize > 0
			? static_cast<quint64>(systemPageSize / 1024) : 4;
		const QDir procDirectory(QStringLiteral("/proc"));
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
			samples.append({group, processDirectory, residentKilobytes});
			residentByGroup[group] += residentKilobytes;
		}

		QVector<QPair<QString, quint64>> residentRanking;
		residentRanking.reserve(residentByGroup.size());
		for (auto it = residentByGroup.cbegin(); it != residentByGroup.cend(); ++it)
			residentRanking.append({it.key(), it.value()});
		std::sort(residentRanking.begin(), residentRanking.end(), [](const auto &left, const auto &right) {
			return left.second > right.second;
		});
		QSet<QString> candidates;
		for (qsizetype i = 0; i < std::min<qsizetype>(12, residentRanking.size()); ++i)
			candidates.insert(residentRanking.at(i).first);

		QHash<QString, quint64> groupedKilobytes;
		for (const ProcessSample &sample : samples) {
			if (!candidates.contains(sample.group))
				continue;
			QFile smaps(sample.directory + QStringLiteral("/smaps_rollup"));
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
			groupedKilobytes[sample.group] += foundPss
				? proportionalKilobytes : sample.residentKilobytes;
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


private:
	QTimer m_memoryTimer;
	QTimer m_processTimer;
	QVariantList m_topProcesses;
	QString m_memorySummary = QStringLiteral("-- / -- GB");
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
	ProcessRamMonitor processRamMonitor;
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
