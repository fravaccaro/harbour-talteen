# NOTICE:
#
# Application name defined in TARGET has a corresponding QML filename.
# If name defined in TARGET is changed, the following needs to be done
# to match new name:
#   - corresponding QML filename must be changed
#   - desktop icon filename must be changed
#   - desktop filename must be changed
#   - icon definition filename in desktop file must be changed
#   - translation filenames have to be changed

# The name of your application
TARGET = harbour-talteen

QT += qml quick dbus
CONFIG += link_pkgconfig
PKGCONFIG += packagekitqt5
LIBS += -lcrypto

images.files = images/*.png
images.path = $$PREFIX/share/$$TARGET/images


CONFIG += sailfishapp

HEADERS += \
    src/spawner.h \
    src/networktransfer.h \
    src/talteen_crypto.h \
    src/talteen.h

SOURCES += \
    src/main.cpp \
    src/spawner.cpp \
    src/networktransfer.cpp \
    src/talteen.cpp \
    src/talteen_device.cpp \
    src/talteen_backup.cpp \
    src/talteen_crypto.cpp \
    src/talteen_restore.cpp \
    src/talteen_archive.cpp

DISTFILES += qml/harbour-talteen.qml \
    qml/cover/*.qml \
    qml/pages/*.qml \
    rpm/harbour-talteen.changes.in \
    rpm/harbour-talteen.changes.run.in \
    rpm/harbour-talteen.spec \
    translations/*.ts \
    harbour-talteen.desktop

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172

# to disable building translations every time, comment out the
# following CONFIG line
CONFIG += sailfishapp_i18n


privileges.files = services/harbour-talteen
privileges.path = $$INSTALL_ROOT/usr/share/mapplauncherd/privileges.d/
dbus.files = services/harbour.talteen.service
dbus.path = $$INSTALL_ROOT/usr/share/dbus-1/services
polkit_rules.files = services/99-talteen.rules
polkit_rules.path = $$INSTALL_ROOT/usr/share/polkit-1/rules.d/

INSTALLS += privileges dbus polkit_rules images
TRANSLATIONS +=  translations/*.ts