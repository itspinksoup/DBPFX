# Windows resources: version information for every binary, channel icons from DBPFX.Identity.dll.

if(DBPFX_VERSION_SUFFIX)
    set(DBPFX_RC_FLAGS "VS_FF_PRERELEASE")
else()
    set(DBPFX_RC_FLAGS "0")
endif()

#   dbpfx_add_version_resource(<target> EXE|DLL "<description>")
function(dbpfx_add_version_resource target kind description)
    get_target_property(_name ${target} OUTPUT_NAME)
    if(NOT _name)
        set(_name ${target})
    endif()
    set(DBPFX_RC_NAME "${_name}")
    set(DBPFX_RC_DESCRIPTION "${description}")
    if(kind STREQUAL "EXE")
        set(DBPFX_RC_FILENAME "${_name}.exe")
        set(DBPFX_RC_FILETYPE "VFT_APP")
    else()
        set(DBPFX_RC_FILENAME "${_name}.dll")
        set(DBPFX_RC_FILETYPE "VFT_DLL")
    endif()
    set(_rc "${CMAKE_CURRENT_BINARY_DIR}/resources/${target}.rc")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in" "${_rc}" @ONLY)
    target_sources(${target} PRIVATE "${_rc}")
endfunction()

#   dbpfx_add_channel_icon(<exe target>)  - copy the channel's icon out of DBPFX.Identity.dll after linking
function(dbpfx_add_channel_icon target)
    add_dependencies(${target} dbpfx-iconstamp dbpfx-identity)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "$<TARGET_FILE:dbpfx-iconstamp>" "$<TARGET_FILE:dbpfx-identity>" ${DBPFX_CHANNEL} "$<TARGET_FILE:${target}>"
        VERBATIM)
endfunction()
