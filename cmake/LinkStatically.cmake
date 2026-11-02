# Makes an executable run without the compiler's DLLs next to it
function(crimson_link_statically target)
    if(MINGW)
        # In C++23 std::type_info::operator== is inline, and also defined in the static libstdc++.a.
        # The two are the same function, so the linker can keep either.
        target_link_options(${target} PRIVATE -static-libgcc -static-libstdc++ -static -Wl,--allow-multiple-definition)
    endif()
endfunction()
