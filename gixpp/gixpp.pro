TEMPLATE = app
TARGET = gixpp

CONFIG(release,debug|release) DESTDIR = ../x64/Release
CONFIG(debug,debug|release) DESTDIR = ../x64/Debug

QT += core xml network gui xmlpatterns concurrent
CONFIG += c++17
INCLUDEPATH += . ./GeneratedFiles ./GeneratedFiles/build \
				 ../libgixutils ../libgixpp

linux:QMAKE_LFLAGS_DEBUG += -rdynamic
linux:QMAKE_CXXFLAGS_DEBUG += -O0 -std=c++17 -Wno-unknown-pragmas
linux:QMAKE_LFLAGS_RELEASE+= 
linux:QMAKE_CXXFLAGS_RELEASE+= -O3 -std=c++17 -Wno-unknown-pragmas
linux:DEFINES += QT_NETWORK_LIB QT_UITOOLS_LIB QT_WIDGETS_LIB QT_XML_LIB QT_XMLPATTERNS_LIB _GCDEBUGGER_NET_DEBUG _DEBUG
linux:INCLUDEPATH +=
linux:LIBPATH += $(DESTDIR) 
linux:LIBS += -lgixpp -lgixutils

win32:DEFINES += _UNICODE WIN64 QT_DLL QT_NETWORK_LIB QT_UITOOLS_LIB QT_WIDGETS_LIB QT_XML_LIB QT_XMLPATTERNS_LIB _HAS_STD_BYTE=0
win32:INCLUDEPATH += ../build-tools/grammar-tools \
win32:RC_FILE =
win32:LIBPATH += $(DESTDIR)
win32:LIBS += -lgixpp -lgixutils
win32:QMAKE_LFLAGS_DEBUG += -rdynamic
win32:QMAKE_CXXFLAGS_DEBUG += -O0 -std=c++17 -Wno-unknown-pragmas
win32:QMAKE_LFLAGS_RELEASE+=
win32:QMAKE_CXXFLAGS_RELEASE+= -O3 -std=c++17 -Wno-unknown-pragmas

macx:LIBS += -F $(HOME)/Projects/scintilla/bin -framework ScintillaEdit -lgixutils -lgixixpp -lgix-common -lgixsql
macx:QMAKE_POST_LINK = mkdir -p ../x64/Debug/gix-ide.app/Contents/Frameworks && cp -frvp ../../scintilla/bin/ScintillaEdit.framework ../x64/Debug/gix-ide.app/Contents/Frameworks
macx:INCLUDEPATH += ../gixsql/libgixsql 
macx:LIBPATH += ../libs/scintilla/src/scintilla/bin ../gix-common $(DESTDIR) 

DEPENDPATH += .
MOC_DIR += GeneratedFiles
OBJECTS_DIR += build
UI_DIR += GeneratedFiles
RCC_DIR += GeneratedFiles

HEADERS = ./linq/linq_selectmany.hpp \
./linq/util.hpp \
./linq/linq_select.hpp \
./linq/linq_last.hpp \
./linq/linq_iterators.hpp \
./linq/linq_take.hpp \
./linq/linq_skip.hpp \
./linq/linq_where.hpp \
./linq/linq_groupby.hpp \
./linq/linq.hpp \
./linq/linq_cursor.hpp \

SOURCES = ./main.cpp