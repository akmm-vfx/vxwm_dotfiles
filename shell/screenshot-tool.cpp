#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QProcess>
#include <QProcessEnvironment>
#include <QVariantList>
#include <QVariantMap>
#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QQmlContext>
#include <QScreen>
#include <QStandardPaths>
#include <QUrl>
#include <QQuickImageProvider>
#include <QQuickView>
#include <QTimer>
#include <QDateTime>
#include <QRect>
#include <QSet>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#undef Bool

class CaptureImageProvider final : public QQuickImageProvider {
public:
	explicit CaptureImageProvider(const QImage &image)
		: QQuickImageProvider(QQuickImageProvider::Image), image_(image) {}

	QImage requestImage(const QString &, QSize *size, const QSize &) override {
		if (size)
			*size = image_.size();
		return image_;
	}

private:
	QImage image_;
};

class ScreenshotController final : public QObject {
	Q_OBJECT
	Q_PROPERTY(int screenX READ screenX CONSTANT)
	Q_PROPERTY(int screenY READ screenY CONSTANT)
	Q_PROPERTY(int screenWidth READ screenWidth CONSTANT)
	Q_PROPERTY(int screenHeight READ screenHeight CONSTANT)
	Q_PROPERTY(QVariantList detectedWindows READ detectedWindows CONSTANT)

public:
	explicit ScreenshotController(QObject *parent = nullptr) : QObject(parent) {
		QScreen *screen = QGuiApplication::primaryScreen();
		if (!screen)
			return;

		const QRect geometry = screen->geometry();
		screenX_ = geometry.x();
		screenY_ = geometry.y();
		screenWidth_ = geometry.width();
		screenHeight_ = geometry.height();
		capture_ = screen->grabWindow(0).toImage();
		detectWindows();
	}

	int screenX() const { return screenX_; }
	int screenY() const { return screenY_; }
	int screenWidth() const { return screenWidth_; }
	int screenHeight() const { return screenHeight_; }
	QImage captureImage() const { return capture_; }
	QVariantList detectedWindows() const { return detectedWindows_; }

	Q_INVOKABLE bool copySelection(int x, int y, int width, int height,
		const QString &shape, const QVariantList &strokes) {
		const QImage selected = composeSelection(x, y, width, height, shape, strokes);
		if (selected.isNull()) {
			emit operationFinished(false, tr("Select an area first"));
			return false;
		}

		const QString path = QDir::home().filePath(QStringLiteral("vxwm/shell/workspace-state/clipboard-%1.png")
			.arg(QCoreApplication::applicationPid()));
		if (!selected.save(path, "PNG")) {
			emit operationFinished(false, tr("Could not encode the image"));
			return false;
		}

		if (!QProcess::startDetached(QCoreApplication::applicationFilePath(),
			{QStringLiteral("--clipboard"), path})) {
			QFile::remove(path);
			emit operationFinished(false, tr("Could not start clipboard owner"));
			return false;
		}
		emit operationFinished(true, tr("Copied image to clipboard"));
		return true;
	}

	Q_INVOKABLE void saveSelection(int x, int y, int width, int height,
		const QString &shape, const QVariantList &strokes) {
		saving_ = composeSelection(x, y, width, height, shape, strokes);
		if (saving_.isNull()) {
			emit operationFinished(false, tr("Select an area first"));
			return;
		}

		const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
		const QString filename = QStringLiteral("Screenshot-%1.png")
			.arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss")));
		const QString initialPath = QDir(pictures).filePath(filename);
		QProcess dialog;
		QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
		environment.insert(QStringLiteral("XDG_CURRENT_DESKTOP"), QStringLiteral("KDE"));
		environment.insert(QStringLiteral("QT_QPA_PLATFORMTHEME"), QStringLiteral("kde"));
		dialog.setProcessEnvironment(environment);
		const QString filter = shape == QStringLiteral("circle")
			? QStringLiteral("PNG image (*.png)")
			: QStringLiteral("Images (*.png *.jpg *.jpeg *.webp)");
		dialog.start(QStringLiteral("/usr/sbin/kdialog"), {QStringLiteral("--title"),
			QStringLiteral("Save screenshot"), QStringLiteral("--getsavefilename"), initialPath,
			filter});
		if (!dialog.waitForStarted(3000) || !dialog.waitForFinished(-1)) {
			emit operationFinished(false, tr("Could not open the KDE save dialog: %1").arg(dialog.errorString()));
			saving_ = QImage();
			return;
		}

		QString path = QString::fromLocal8Bit(dialog.readAllStandardOutput()).trimmed();
		if (path.isEmpty()) {
			emit operationFinished(false, QString());
			saving_ = QImage();
			return;
		}
		if (QFileInfo(path).suffix().isEmpty())
			path += QStringLiteral(".png");
		const QByteArray format = QFileInfo(path).suffix().toLatin1();
		if (!saving_.save(path, format.constData())) {
			emit operationFinished(false, tr("Could not save the image"));
			saving_ = QImage();
			return;
		}
		saving_ = QImage();
		emit operationFinished(true, tr("Screenshot saved"));
		emit closeRequested();
	}

	Q_INVOKABLE void closeOverlay() { emit closeRequested(); }
	Q_INVOKABLE void hideOverlay() {
		if (view_)
			view_->hide();
	}
	Q_INVOKABLE void showOverlay() {
		if (view_) {
			view_->showFullScreen();
			view_->raise();
			view_->requestActivate();
		}
	}
	void setView(QQuickView *view) { view_ = view; }

signals:
	void operationFinished(bool success, const QString &message);
	void closeRequested();

private:
	static QString appIcon(const QString &className)
	{
		const QString name = className.toLower();
		if (name.contains(QStringLiteral("zen"))) return QStringLiteral("zen-browser");
		if (name.contains(QStringLiteral("firefox"))) return QStringLiteral("firefox");
		if (name.contains(QStringLiteral("chrom")) || name.contains(QStringLiteral("brave")))
			return QStringLiteral("chromium-browser");
		if (name.contains(QStringLiteral("kitty")) || name.contains(QStringLiteral("terminal")))
			return QStringLiteral("kitty");
		if (name.contains(QStringLiteral("dolphin"))) return QStringLiteral("org.kde.dolphin");
		if (name.contains(QStringLiteral("blender"))) return QStringLiteral("blender");
		if (name.contains(QStringLiteral("chatgpt"))) return QStringLiteral("chatgpt");
		return QStringLiteral("application-x-executable");
	}

	static QString windowTitle(Display *display, Window window)
	{
		const Atom nameAtom = XInternAtom(display, "_NET_WM_NAME", False);
		const Atom utf8Atom = XInternAtom(display, "UTF8_STRING", False);
		Atom actualType;
		int actualFormat;
		unsigned long count, remaining;
		unsigned char *data = nullptr;
		QString title;
		if (XGetWindowProperty(display, window, nameAtom, 0, 2048, False,
			utf8Atom, &actualType, &actualFormat, &count, &remaining, &data) == Success &&
			data && actualFormat == 8)
			title = QString::fromUtf8(reinterpret_cast<const char *>(data),
				static_cast<qsizetype>(count));
		if (data)
			XFree(data);
		if (title.isEmpty()) {
			char *legacy = nullptr;
			if (XFetchName(display, window, &legacy) && legacy)
				title = QString::fromLocal8Bit(legacy);
			if (legacy)
				XFree(legacy);
		}
		return title.trimmed();
	}

	void detectWindows()
	{
		Display *display = XOpenDisplay(nullptr);
		if (!display)
			return;
		const Window root = DefaultRootWindow(display);
		const Atom clientsAtom = XInternAtom(display, "_NET_CLIENT_LIST", False);
		Atom actualType;
		int actualFormat;
		unsigned long count, remaining;
		unsigned char *data = nullptr;
		QSet<Window> clients;
		if (XGetWindowProperty(display, root, clientsAtom, 0, 4096, False,
			XA_WINDOW, &actualType, &actualFormat, &count, &remaining, &data) == Success &&
			data && actualFormat == 32) {
			const Window *ids = reinterpret_cast<const Window *>(data);
			for (unsigned long i = 0; i < count; ++i)
				clients.insert(ids[i]);
		}
		if (data)
			XFree(data);

		Window returnedRoot, parent, *children = nullptr;
		unsigned int childCount = 0;
		if (!XQueryTree(display, root, &returnedRoot, &parent, &children, &childCount)) {
			XCloseDisplay(display);
			return;
		}
		for (int i = static_cast<int>(childCount) - 1; i >= 0; --i) {
			const Window window = children[i];
			if (!clients.contains(window))
				continue;
			XWindowAttributes attributes;
			if (!XGetWindowAttributes(display, window, &attributes) ||
				attributes.map_state != IsViewable || attributes.override_redirect)
				continue;
			XClassHint hint{};
			QString className;
			if (XGetClassHint(display, window, &hint)) {
				if (hint.res_class)
					className = QString::fromLocal8Bit(hint.res_class);
				if (hint.res_name) {
					XFree(hint.res_name);
				}
				if (hint.res_class)
					XFree(hint.res_class);
			}
			if (className.compare(QStringLiteral("vxwm-shell"), Qt::CaseInsensitive) == 0)
				continue;
			int rootX = 0, rootY = 0;
			Window child;
			XTranslateCoordinates(display, window, root, 0, 0, &rootX, &rootY, &child);
			QRect bounds(rootX - screenX_, rootY - screenY_,
				attributes.width, attributes.height);
			bounds = bounds.intersected(QRect(0, 0, screenWidth_, screenHeight_));
			if (bounds.width() < 8 || bounds.height() < 8)
				continue;
			QString appName = className.isEmpty() ? QStringLiteral("Application") : className;
			const QString lower = className.toLower();
			if (lower.contains(QStringLiteral("kitty"))) appName = QStringLiteral("Kitty");
			else if (lower.contains(QStringLiteral("dolphin"))) appName = QStringLiteral("Dolphin");
			else if (lower.contains(QStringLiteral("zen"))) appName = QStringLiteral("Zen Browser");
			else if (lower.contains(QStringLiteral("firefox"))) appName = QStringLiteral("Firefox");
			else if (lower.contains(QStringLiteral("blender"))) appName = QStringLiteral("Blender");
			else if (lower.contains(QStringLiteral("chatgpt"))) appName = QStringLiteral("ChatGPT");
			detectedWindows_.append(QVariantMap{
				{QStringLiteral("appName"), appName},
				{QStringLiteral("title"), windowTitle(display, window)},
				{QStringLiteral("iconName"), appIcon(className)},
				{QStringLiteral("windowId"), QString::number(window, 16)},
				{QStringLiteral("x"), bounds.x()},
				{QStringLiteral("y"), bounds.y()},
				{QStringLiteral("width"), bounds.width()},
				{QStringLiteral("height"), bounds.height()}
			});
		}
		if (children)
			XFree(children);
		XCloseDisplay(display);
	}

	QImage composeSelection(int x, int y, int width, int height,
		const QString &shape, const QVariantList &strokes) const {
		QImage selected = cropSelection(x, y, width, height);
		if (selected.isNull())
			return selected;

		if (!strokes.isEmpty()) {
			const qreal scaleX = qreal(capture_.width()) / screenWidth_;
			const qreal scaleY = qreal(capture_.height()) / screenHeight_;
			const int originX = qRound(x * scaleX);
			const int originY = qRound(y * scaleY);
			QPainter painter(&selected);
			painter.setRenderHint(QPainter::Antialiasing);
			painter.setClipRect(selected.rect());
			for (const QVariant &value : strokes) {
				const QVariantMap stroke = value.toMap();
				const QVariantList points = stroke.value(QStringLiteral("points")).toList();
				if (points.isEmpty())
					continue;

				const QColor color(stroke.value(QStringLiteral("color")).toString());
				const qreal penWidth = stroke.value(QStringLiteral("width")).toReal() * (scaleX + scaleY) / 2;
				if (!color.isValid() || penWidth <= 0)
					continue;
				painter.setPen(QPen(color, penWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
				const auto point = [scaleX, scaleY, originX, originY](const QVariant &value) {
					const QVariantMap p = value.toMap();
					return QPointF(qRound(p.value(QStringLiteral("x")).toReal() * scaleX) - originX,
						qRound(p.value(QStringLiteral("y")).toReal() * scaleY) - originY);
				};
				QPainterPath path;
				path.moveTo(point(points.first()));
				for (qsizetype i = 1; i < points.size(); ++i)
					path.lineTo(point(points.at(i)));
				painter.drawPath(path);
			}
			painter.end();
		}
		if (shape == QStringLiteral("circle")) {
			QImage masked(selected.size(), QImage::Format_ARGB32_Premultiplied);
			masked.fill(Qt::transparent);
			QPainter maskPainter(&masked);
			maskPainter.setRenderHint(QPainter::Antialiasing);
			QPainterPath circle;
			circle.addEllipse(QRectF(masked.rect()));
			maskPainter.setClipPath(circle);
			maskPainter.drawImage(0, 0, selected);
			selected = masked;
		}
		return selected;
	}

	QImage cropSelection(int x, int y, int width, int height) const {
		if (capture_.isNull() || width < 2 || height < 2 || screenWidth_ < 1 || screenHeight_ < 1)
			return {};

		const qreal scaleX = qreal(capture_.width()) / screenWidth_;
		const qreal scaleY = qreal(capture_.height()) / screenHeight_;
		const QRect pixels(qRound(x * scaleX), qRound(y * scaleY),
			qRound(width * scaleX), qRound(height * scaleY));
		return capture_.copy(pixels.intersected(capture_.rect()));
	}

	QImage capture_;
	QImage saving_;
	QVariantList detectedWindows_;
	int screenX_ = 0;
	int screenY_ = 0;
	int screenWidth_ = 0;
	int screenHeight_ = 0;
	QQuickView *view_ = nullptr;
};

int main(int argc, char **argv) {
	QGuiApplication app(argc, argv);
	app.setApplicationName(QStringLiteral("vxwm-screenshot"));
	const QStringList arguments = app.arguments();
	if (arguments.size() == 3 && arguments.at(1) == QStringLiteral("--clipboard")) {
		const QString path = arguments.at(2);
		const QImage image(path);
		if (image.isNull())
			return 1;
		QClipboard *clipboard = app.clipboard();
		bool ready = false;
		QObject::connect(clipboard, &QClipboard::dataChanged, &app, [&] {
			if (ready && !clipboard->ownsClipboard())
				app.quit();
		});
		clipboard->setImage(image, QClipboard::Clipboard);
		QTimer::singleShot(250, &app, [&] {
			ready = true;
			if (!clipboard->ownsClipboard())
				app.quit();
		});
		const int result = app.exec();
		QFile::remove(path);
		return result;
	}
	const QString lockMarker = QDir::home().filePath(
		QStringLiteral("vxwm/shell/workspace-state/lock-screen-open"));
	if (QFile::exists(lockMarker))
		return 0;

	QScreen *screen = QGuiApplication::primaryScreen();
	if (!screen)
		return 1;
	QQuickView view;
	ScreenshotController controller;
	view.engine()->addImageProvider(QStringLiteral("capture"), new CaptureImageProvider(controller.captureImage()));
	view.rootContext()->setContextProperty(QStringLiteral("captureController"), &controller);
	const QString qmlPath = QDir::home().filePath(QStringLiteral("vxwm/shell/ScreenshotOverlay.qml"));
	view.setSource(QUrl::fromLocalFile(qmlPath));
	if (view.status() == QQuickView::Error)
		return 1;
	controller.setView(&view);
	view.setTitle(QStringLiteral("VXWM Screenshot Overlay"));
	QObject::connect(&controller, &ScreenshotController::closeRequested, &app, &QGuiApplication::quit);
	view.setScreen(screen);
	view.setFlags(Qt::FramelessWindowHint | Qt::BypassWindowManagerHint | Qt::WindowStaysOnTopHint);
	view.setGeometry(screen->geometry());
	view.showFullScreen();
	view.raise();
	view.requestActivate();
	QTimer stackingGuard;
	stackingGuard.setInterval(75);
	QObject::connect(&stackingGuard, &QTimer::timeout, &app, [&] {
		if (QFile::exists(lockMarker)) {
			view.hide();
			app.quit();
			return;
		}
		if (!view.isVisible())
			return;
		view.raise();
		if (!view.isActive())
			view.requestActivate();
	});
	stackingGuard.start();
	return app.exec();
}

#include "screenshot-tool.moc"
