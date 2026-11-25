# Link an existing consumer target to the independent native localization API.
function(iilocalizer_link target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "iilocalizer_link requires an existing target")
    endif()
    target_link_libraries("${target}" PRIVATE iiLocalizer::iiLocalizer)
endfunction()
