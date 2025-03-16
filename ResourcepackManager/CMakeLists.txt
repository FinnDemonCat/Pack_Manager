cmake_minimum_required(VERSION 3.10)
project(ResourcepackManager VERSION 1.0)
set(CPACK_NSIS_INSTALL_ROOT "C:")  
set(CPACK_PACKAGE_INSTALL_DIRECTORY "ResourcepackManager")

# Copiando executável
install(PROGRAMS ScanRP2.exe DESTINATION .)

# Copiando a DLL necessária
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/pdcurses.dll DESTINATION .)

# Copiando pastas base
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lang DESTINATION .)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/templates DESTINATION .)

# Instalar as pastas vazias
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/log DESTINATION . PATTERN "*" EXCLUDE)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/resourcepacks DESTINATION . PATTERN "*" EXCLUDE)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/instructions DESTINATION . PATTERN "*" EXCLUDE)

set(CPACK_NSIS_CREATE_ICONS_EXTRA "
    CreateShortCut \\\"$DESKTOP\\\\ResourcepackManager.lnk\\\" \\\"$INSTDIR\\\\ScanRP2.exe\\\"\\n
")

set(CPACK_NSIS_DELETE_ICONS_EXTRA "
	Delete \\\"$DESKTOP\\\\ResourcepackManager.lnk\\\"\\n
	Delete \\\"$INSTDIR\\\\ScanRP2.exe\\\"\\n
	RMDir \\\"$INSTDIR\\\\log\\\"\\n
	RMDir \\\"$INSTDIR\\\\resourcepacks\\\"\\n
	RMDir \\\"$INSTDIR\\\\instructions\\\"\\n
	RMDir \\\"$INSTDIR\\\"\\n
")

set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
    CreateDirectory \\\"$INSTDIR\\\\log\\\"
    CreateDirectory \\\"$INSTDIR\\\\resourcepacks\\\"
    CreateDirectory \\\"$INSTDIR\\\\instructions\\\"
")

include(CPack)
set (CPACK_NSIS_MODIFY_PATH "ON")
set(CPACK_PACKAGE_NAME "Resourcepack Manager")
set(CPACK_PACKAGE_VERSION "1.0")
set(CPACK_PACKAGE_CONTACT "finndemoncat@gmail.com")

if(WIN32)
	set(CPACK_GENERATOR "NSIS")  # Cria um instalador .exe no Windows
endif()

set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL OFF)  # Evita perguntas desnecessárias
set(CPACK_RESOURCE_FILE_LICENSE "")  # Remove a tela de licença
set(CPACK_PACKAGE_DESCRIPTION "Instalador do Resourcepack Manager")