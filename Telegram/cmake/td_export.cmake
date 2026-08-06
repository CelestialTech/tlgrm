# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_library(td_export OBJECT)
init_non_host_target(td_export)
add_library(tdesktop::td_export ALIAS td_export)

target_precompile_headers(td_export PRIVATE ${src_loc}/export/export_pch.h)
nice_target_sources(td_export ${src_loc}
PRIVATE
    export/export_api_wrap.cpp
    export/export_api_wrap.h
    export/export_controller.cpp
    export/export_resume_state.cpp
    export/export_resume_state.h
    export/export_controller.h
    export/export_pch.h
    export/export_settings.cpp
    export/export_settings.h
    export/data/export_data_types.cpp
    export/data/export_data_types.h
    export/output/export_output_abstract.cpp
    export/output/export_output_abstract.h
    export/output/export_output_file.cpp
    export/output/export_output_file.h
    export/output/export_output_html.cpp
    export/output/export_output_html.h
    export/output/export_output_html_and_json.cpp
    export/output/export_output_html_and_json.h
    export/output/export_output_json.cpp
    export/output/export_output_json.h
    export/output/export_output_result.h
    export/output/export_output_stats.cpp
    export/output/export_output_stats.h
)

target_include_directories(td_export
PUBLIC
    ${src_loc}
)

target_link_libraries(td_export
PUBLIC
    desktop-app::lib_base
    tdesktop::td_scheme
)

# Headless tests for the resume record.
#
# A separate executable rather than a scenario in the in-app harness: that one
# is #ifdef _DEBUG, its repository copy has to stay a no-op, and it needs a
# live session. A resume record is pure data, so its tests should need nothing
# but Qt Core -- tests that require an account are tests nobody runs.
#
# Build and run:  cmake --build . --target tlgrm_export_tests && ./tlgrm_export_tests
if (NOT DESKTOP_APP_DISABLE_TESTS)
    add_executable(tlgrm_export_tests
        ${src_loc}/export/export_resume_state.cpp
        ${src_loc}/export/export_resume_state_tests.cpp
    )
    init_non_host_target(tlgrm_export_tests)
    target_include_directories(tlgrm_export_tests PRIVATE ${src_loc})
    target_link_libraries(tlgrm_export_tests
    PRIVATE
        desktop-app::external_qt
        desktop-app::lib_base
    )
    add_test(NAME tlgrm_export_tests COMMAND tlgrm_export_tests)
endif()
