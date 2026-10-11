# Test-only fixtures for each existing VSTProcess architecture.
function(wjn_add_vst_block_fixtures suffix root output)
    set(sdk "${root}/third_party/vst3sdk")
    add_library(wjn_vst2_block_fixture${suffix} SHARED "${root}/Develop/Vst/Vst2BlockFixture.cpp")
    target_include_directories(wjn_vst2_block_fixture${suffix} PRIVATE "${root}/third_party/vst2sdk")
    add_library(wjn_vst3_block_fixture${suffix} SHARED
        "${root}/Develop/Vst/Vst3BlockFixture.cpp"
        "${root}/Develop/Vst/Vst3BlockFixture.def"
        "${sdk}/pluginterfaces/base/funknown.cpp"
        "${sdk}/pluginterfaces/base/coreiids.cpp"
        "${sdk}/pluginterfaces/base/ustring.cpp"
        "${sdk}/base/source/baseiids.cpp"
        "${sdk}/base/source/fobject.cpp"
        "${sdk}/base/source/fstring.cpp"
        "${sdk}/base/source/fbuffer.cpp"
        "${sdk}/base/source/fdebug.cpp"
        "${sdk}/base/source/updatehandler.cpp"
        "${sdk}/base/thread/source/flock.cpp"
        "${sdk}/public.sdk/source/main/pluginfactory.cpp"
        "${sdk}/public.sdk/source/vst/vstsinglecomponenteffect.cpp"
        "${sdk}/public.sdk/source/common/pluginview.cpp"
        "${sdk}/public.sdk/source/common/commoniids.cpp"
        "${sdk}/public.sdk/source/vst/vstcomponentbase.cpp"
        "${sdk}/public.sdk/source/vst/vstbus.cpp"
        "${sdk}/public.sdk/source/vst/vstparameters.cpp"
        "${sdk}/public.sdk/source/vst/vstinitiids.cpp")
    target_include_directories(wjn_vst3_block_fixture${suffix} PRIVATE "${sdk}")
    target_compile_definitions(wjn_vst3_block_fixture${suffix} PRIVATE RELEASE=1)
    foreach(type IN ITEMS 2 3)
        set(target wjn_vst${type}_block_fixture${suffix})
        target_compile_features(${target} PRIVATE cxx_std_17)
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${output}/tests/vst-s5"
            OUTPUT_NAME "Vst${type}BlockFixture${suffix}")
    endforeach()
    set_target_properties(wjn_vst3_block_fixture${suffix} PROPERTIES SUFFIX ".vst3")
    add_library(wjn_vst_jack_fixture${suffix} SHARED "${root}/Develop/Vst/JackFixture.cpp"
                                                  "${root}/Develop/Vst/JackFixture.def")
    target_include_directories(wjn_vst_jack_fixture${suffix} PRIVATE "${root}/third_party/JACK2/include")
    target_compile_features(wjn_vst_jack_fixture${suffix} PRIVATE cxx_std_17)
    target_compile_definitions(wjn_vst_jack_fixture${suffix} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    set_target_properties(wjn_vst_jack_fixture${suffix} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${output}/tests/vst-s5"
        OUTPUT_NAME "JackFixture${suffix}")
endfunction()
