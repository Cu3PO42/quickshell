find_package(PkgConfig REQUIRED)
pkg_check_modules(polkit REQUIRED IMPORTED_TARGET polkit-qt6-core-1 polkit-qt6-agent-1)

qt_add_library(quickshell-service-polkit STATIC
	qml.cpp
)

qt_add_qml_module(quickshell-service-polkit
	URI Quickshell.Services.Polkit
	VERSION 0.1
	DEPENDENCIES QtQml
)

install_qml_module(quickshell-service-polkit)

target_link_libraries(quickshell-service-polkit PRIVATE
	Qt::Qml PkgConfig::polkit
	Qt::Quick # pch
)

qs_module_pch(quickshell-service-polkit)

target_link_libraries(quickshell PRIVATE quickshell-service-polkitplugin)
