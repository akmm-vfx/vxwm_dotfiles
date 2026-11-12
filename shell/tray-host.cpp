#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QImage>
#include <QObject>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QVariant>
#include <QtEndian>

struct SniPixmap {
    int width = 0;
    int height = 0;
    QByteArray data;
};

using SniPixmaps = QList<SniPixmap>;

Q_DECLARE_METATYPE(SniPixmap)
Q_DECLARE_METATYPE(SniPixmaps)

QDBusArgument &operator<<(QDBusArgument &argument, const SniPixmap &pixmap)
{
    argument.beginStructure();
    argument << pixmap.width << pixmap.height << pixmap.data;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, SniPixmap &pixmap)
{
    argument.beginStructure();
    argument >> pixmap.width >> pixmap.height >> pixmap.data;
    argument.endStructure();
    return argument;
}

class TrayHost;

class TrayControlObject final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.vxwm.TrayHost")

public:
    explicit TrayControlObject(TrayHost *host) : host(host) {}

public slots:
    void InvokeItem(int id, bool contextMenu, int x, int y);

private:
    TrayHost *host;
};

class WatcherObject final : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierWatcher")
    Q_PROPERTY(QStringList RegisteredStatusNotifierItems READ registeredItems)
    Q_PROPERTY(bool IsStatusNotifierHostRegistered READ hostRegistered)
    Q_PROPERTY(int ProtocolVersion READ protocolVersion)

public:
    explicit WatcherObject(TrayHost *host);
    QStringList registeredItems() const;
    bool hostRegistered() const { return true; }
    int protocolVersion() const { return 0; }

public slots:
    void RegisterStatusNotifierItem(const QString &service);
    void RegisterStatusNotifierHost(const QString &service);

signals:
    void StatusNotifierItemRegistered(const QString &service);
    void StatusNotifierItemUnregistered(const QString &service);
    void StatusNotifierHostRegistered();
    void StatusNotifierHostUnregistered();

private:
    TrayHost *host;
};

class TrayHost final : public QObject {
    Q_OBJECT

public:
    struct Item {
        QString service;
        QString path;
        QString id;
        QString iconFile;
    };

    explicit TrayHost(QString stateDirectory)
        : stateDirectory(std::move(stateDirectory)), bus(QDBusConnection::sessionBus())
    {
        qDBusRegisterMetaType<SniPixmap>();
        qDBusRegisterMetaType<SniPixmaps>();
        QDir().mkpath(this->stateDirectory);
    }

    bool start()
    {
        if (!bus.isConnected())
            return false;
        const QString watcherService = QStringLiteral("org.kde.StatusNotifierWatcher");
        const QString watcherPath = QStringLiteral("/StatusNotifierWatcher");
        const QString watcherInterface = QStringLiteral("org.kde.StatusNotifierWatcher");
        if (bus.registerService(watcherService)) {
            auto *watcher = new WatcherObject(this);
            if (!bus.registerObject(watcherPath, watcher,
                                    QDBusConnection::ExportAllSlots |
                                    QDBusConnection::ExportAllProperties |
                                    QDBusConnection::ExportAllSignals))
                return false;
            bus.connect(QStringLiteral("org.freedesktop.DBus"),
                        QStringLiteral("/org/freedesktop/DBus"),
                        QStringLiteral("org.freedesktop.DBus"),
                        QStringLiteral("NameOwnerChanged"), this,
                        SLOT(handleNameOwnerChanged(QString,QString,QString)));
        } else {
            bus.connect(watcherService, watcherPath, watcherInterface,
                        QStringLiteral("StatusNotifierItemRegistered"), this,
                        SLOT(handleItemRegistered(QString)));
            bus.connect(watcherService, watcherPath, watcherInterface,
                        QStringLiteral("StatusNotifierItemUnregistered"), this,
                        SLOT(handleItemUnregistered(QString)));
            QDBusMessage registerHost = QDBusMessage::createMethodCall(
                watcherService, watcherPath, watcherInterface,
                QStringLiteral("RegisterStatusNotifierHost"));
            registerHost << bus.baseService();
            if (bus.call(registerHost, QDBus::Block, 1000).type() == QDBusMessage::ErrorMessage)
                return false;

            QDBusMessage getItems = QDBusMessage::createMethodCall(
                watcherService, watcherPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                QStringLiteral("Get"));
            getItems << watcherInterface << QStringLiteral("RegisteredStatusNotifierItems");
            const QDBusMessage reply = bus.call(getItems, QDBus::Block, 1000);
            if (reply.type() != QDBusMessage::ErrorMessage && !reply.arguments().isEmpty()) {
                const QStringList registered = qvariant_cast<QDBusVariant>(reply.arguments().first())
                    .variant().toStringList();
                for (const QString &item : registered)
                    registerItem(item, QString());
            }
        }
        if (!bus.registerService(QStringLiteral("org.vxwm.TrayHost")))
            return false;
        auto *control = new TrayControlObject(this);
        if (!bus.registerObject(QStringLiteral("/TrayHost"), control,
                                QDBusConnection::ExportAllSlots))
            return false;
        QFile ready(stateDirectory + QStringLiteral("/.tray-ready"));
        if (ready.open(QIODevice::WriteOnly))
            ready.close();
        return true;
    }

    QStringList registeredItems() const
    {
        QStringList result;
        for (const Item &item : items)
            result.append(item.service + item.path);
        return result;
    }

    void invokeItem(int id, bool contextMenu, int x, int y)
    {
        const QString itemId = QString::number(id);
        for (const Item &item : items) {
            if (item.id != itemId)
                continue;
            if (contextMenu) {
                const QVariant menuValue = readProperty(item, QStringLiteral("Menu"));
                const QString menuPath = menuValue.canConvert<QDBusObjectPath>()
                    ? menuValue.value<QDBusObjectPath>().path() : menuValue.toString();
                const QString menuHelper = QCoreApplication::applicationDirPath()
                    + QStringLiteral("/tray-menu");
                if (!menuPath.isEmpty() && menuPath != QStringLiteral("/")) {
                    if (QProcess::startDetached(menuHelper,
                            {item.service, menuPath, QString::number(x), QString::number(y)}))
                        return;
                }
            }
            QDBusMessage call = QDBusMessage::createMethodCall(
                item.service, item.path, QStringLiteral("org.kde.StatusNotifierItem"),
                contextMenu ? QStringLiteral("ContextMenu") : QStringLiteral("Activate"));
            call << x << y;
            bus.call(call, QDBus::NoBlock);
            return;
        }
    }

    void registerItem(const QString &serviceArgument, const QString &sender)
    {
        QString service = serviceArgument;
        QString path = QStringLiteral("/StatusNotifierItem");
        if (service.startsWith(QLatin1Char('/'))) {
            path = service;
            service = sender;
        } else if (service.contains(QLatin1Char('/'))) {
            const qsizetype slash = service.indexOf(QLatin1Char('/'));
            path = service.mid(slash);
            service.truncate(slash);
        }
        if (service.isEmpty() || path.isEmpty())
            return;
        for (const Item &item : items)
            if (item.service == service && item.path == path)
                return;

        Item item;
        item.service = service;
        item.path = path;
        item.id = QString::number(nextId++);
        items.append(item);
        bus.connect(service, path, QStringLiteral("org.kde.StatusNotifierItem"),
                    QStringLiteral("NewIcon"), this, SLOT(refreshAll()));
        bus.connect(service, path, QStringLiteral("org.kde.StatusNotifierItem"),
                    QStringLiteral("NewIconThemePath"), this, SLOT(refreshAll()));
        bus.connect(service, path, QStringLiteral("org.kde.StatusNotifierItem"),
                    QStringLiteral("NewAttentionIcon"), this, SLOT(refreshAll()));
        bus.connect(service, path, QStringLiteral("org.kde.StatusNotifierItem"),
                    QStringLiteral("NewStatus"), this, SLOT(refreshWithStatus(QString)));
        bus.connect(service, path, QStringLiteral("org.kde.StatusNotifierItem"),
                    QStringLiteral("NewTitle"), this, SLOT(refreshAll()));
        emit itemRegistered(service + path);
        const QString itemId = item.id;
        QTimer::singleShot(0, this, [this, itemId] {
            for (qsizetype i = 0; i < items.size(); ++i)
                if (items[i].id == itemId) {
                    refreshItem(i);
                    break;
                }
        });
    }

    void registerHost(const QString &)
    {
        emit hostRegistered();
    }

public slots:
    void handleItemRegistered(const QString &servicePath) { registerItem(servicePath, QString()); }
    void handleItemUnregistered(const QString &servicePath) { unregisterItem(servicePath); }

    void refreshAll()
    {
        for (qsizetype i = 0; i < items.size(); ++i)
            refreshItem(i);
    }

    void refreshWithStatus(const QString &) { refreshAll(); }

    void handleNameOwnerChanged(const QString &name, const QString &oldOwner,
                                const QString &newOwner)
    {
        if (!newOwner.isEmpty())
            return;
        for (qsizetype i = items.size() - 1; i >= 0; --i) {
            const Item &item = items[i];
            if (item.service == name || item.service == oldOwner)
                unregisterItem(item.service + item.path);
        }
    }

    void unregisterItem(const QString &servicePath)
    {
        for (qsizetype i = items.size() - 1; i >= 0; --i) {
            Item &item = items[i];
            if (item.service + item.path != servicePath)
                continue;
            bus.disconnect(item.service, item.path,
                           QStringLiteral("org.kde.StatusNotifierItem"),
                           QString(), this, SLOT(refreshAll()));
            if (!item.iconFile.isEmpty())
                QFile::remove(item.iconFile);
            items.removeAt(i);
            emit itemUnregistered(servicePath);
            break;
        }
    }

signals:
    void itemRegistered(const QString &servicePath);
    void itemUnregistered(const QString &servicePath);
    void hostRegistered();

private:
    QVariant readProperty(const Item &item, const QString &property) const
    {
        QDBusMessage call = QDBusMessage::createMethodCall(
            item.service, item.path, QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("Get"));
        call << QStringLiteral("org.kde.StatusNotifierItem") << property;
        const QDBusMessage reply = bus.call(call, QDBus::Block, 500);
        if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) {
            call.setArguments({QStringLiteral("org.freedesktop.StatusNotifierItem"), property});
            const QDBusMessage fallback = bus.call(call, QDBus::Block, 500);
            if (fallback.type() != QDBusMessage::ReplyMessage || fallback.arguments().isEmpty())
                return {};
            const QVariant argument = fallback.arguments().first();
            if (argument.canConvert<QDBusVariant>())
                return qvariant_cast<QDBusVariant>(argument).variant();
            return argument;
        }
        if (reply.type() != QDBusMessage::ReplyMessage)
            return {};
        const QVariant argument = reply.arguments().first();
        if (argument.canConvert<QDBusVariant>())
            return qvariant_cast<QDBusVariant>(argument).variant();
        return argument;
    }

    QImage pixmapImage(const QVariant &variant) const
    {
        SniPixmaps pixmaps;
        if (variant.canConvert<SniPixmaps>())
            pixmaps = qvariant_cast<SniPixmaps>(variant);
        else if (variant.canConvert<QDBusArgument>())
            qvariant_cast<QDBusArgument>(variant) >> pixmaps;

        const SniPixmap *best = nullptr;
        for (const SniPixmap &pixmap : pixmaps)
            if (pixmap.width > 0 && pixmap.height > 0 &&
                pixmap.data.size() >= pixmap.width * pixmap.height * 4 &&
                (!best || qAbs(pixmap.width - 32) < qAbs(best->width - 32)))
                best = &pixmap;
        if (!best)
            return {};

        QImage image(best->width, best->height, QImage::Format_ARGB32);
        const auto *pixels = reinterpret_cast<const uchar *>(best->data.constData());
        for (int y = 0; y < best->height; ++y) {
            auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < best->width; ++x) {
                const int index = (y * best->width + x) * 4;
                const quint32 argb = qFromBigEndian<quint32>(pixels + index);
                line[x] = argb;
            }
        }
        return image;
    }

    void refreshItem(qsizetype index)
    {
        if (index < 0 || index >= items.size())
            return;
        Item &item = items[index];
        const QString iconName = readProperty(item, QStringLiteral("IconName")).toString();
        QImage image;
        if (!iconName.isEmpty())
            image = QIcon::fromTheme(iconName).pixmap(32, 32).toImage();
        if (image.isNull())
            image = pixmapImage(readProperty(item, QStringLiteral("IconPixmap")));
        if (image.isNull())
            return;

        const QString nextFile = stateDirectory + QStringLiteral("/tray-%1.png").arg(item.id);
        const QString tempFile = nextFile + QStringLiteral(".tmp.png");
        if (!image.save(tempFile, "PNG")) {
            return;
        }
        QFile::remove(nextFile);
        QFile::rename(tempFile, nextFile);
        item.iconFile = nextFile;
    }

    QString stateDirectory;
    QDBusConnection bus;
    QList<Item> items;
    int nextId = 1;
};

WatcherObject::WatcherObject(TrayHost *host)
    : QObject(host), host(host)
{
    connect(host, &TrayHost::itemRegistered, this, &WatcherObject::StatusNotifierItemRegistered);
    connect(host, &TrayHost::itemUnregistered, this, &WatcherObject::StatusNotifierItemUnregistered);
    connect(host, &TrayHost::hostRegistered, this, &WatcherObject::StatusNotifierHostRegistered);
}

void TrayControlObject::InvokeItem(int id, bool contextMenu, int x, int y)
{
    host->invokeItem(id, contextMenu, x, y);
}

QStringList WatcherObject::registeredItems() const
{
    return host->registeredItems();
}

void WatcherObject::RegisterStatusNotifierItem(const QString &service)
{
    host->registerItem(service, message().service());
}

void WatcherObject::RegisterStatusNotifierHost(const QString &service)
{
    host->registerHost(service);
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc != 2)
        return 2;
    QIcon::setThemeSearchPaths({QDir::homePath() + QStringLiteral("/.icons"),
                                QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                                    + QStringLiteral("/icons"),
                                QStringLiteral("/usr/local/share/icons"),
                                QStringLiteral("/usr/share/icons")});
    QSettings qtSettings(QDir::homePath() + QStringLiteral("/.config/qt6ct/qt6ct.conf"),
                         QSettings::IniFormat);
    QIcon::setThemeName(qtSettings.value(QStringLiteral("Appearance/icon_theme"),
                                         QStringLiteral("hicolor")).toString());
    QIcon::setFallbackSearchPaths({QStringLiteral("/usr/share/pixmaps")});
    TrayHost host(QString::fromLocal8Bit(argv[1]));
    if (!host.start())
        return 1;
    return app.exec();
}

#include <tray-host.moc>
