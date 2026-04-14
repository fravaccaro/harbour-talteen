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


images.files = images/*.png
images.path = $$PREFIX/share/$$TARGET/images


CONFIG += sailfishapp

HEADERS += \
    src/spawner.h \
    src/networktransfer.h \
    src/talteen.h

SOURCES += \
    src/main.cpp \
    src/spawner.cpp \
    src/networktransfer.cpp \
    src/talteen.cpp

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


INSTALLS += images
TRANSLATIONS +=  translations/*.ts