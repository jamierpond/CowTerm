# Writes Snapshots.txt: the screen each corpus recording replays to, which
# Test/FuzzCorpusReplayTests.cpp checks. Run by Scripts/update-fuzz-snapshots.
add_executable(CowTermWriteFuzzSnapshots WriteSnapshots.cpp)
target_link_libraries(CowTermWriteFuzzSnapshots PRIVATE CowTerm::Core)

# libFuzzer target for TermParser::feed and the screen it drives, under
# AddressSanitizer and UndefinedBehaviorSanitizer. Needs a Clang that ships
# libFuzzer (Apple's does not); Scripts/run-parser-fuzzer configures it.
option(COWTERM_ENABLE_FUZZING "Build the TermParser libFuzzer target" OFF)

if (COWTERM_ENABLE_FUZZING)
    set(COWTERM_SANITIZER_FLAGS -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all)

    add_executable(CowTermParserFuzzer
            ParserFuzzer.cpp
            ../Lib/CowTermCore/CharWidth.cpp
            ../Lib/CowTermCore/GraphemeBreak.cpp
            ../Lib/CowTermCore/CsiParameters.cpp
            ../Lib/CowTermCore/HyperlinkTable.cpp
            ../Lib/CowTermCore/KittyKeyboardFlags.cpp
            ../Lib/CowTermCore/Reflow.cpp
            ../Lib/CowTermCore/SelectGraphicRendition.cpp
            ../Lib/CowTermCore/TermParser.cpp
            ../Lib/CowTermCore/TermScreen.cpp)

    target_compile_options(CowTermParserFuzzer PRIVATE ${COWTERM_SANITIZER_FLAGS} -g -O1)
    target_link_options(CowTermParserFuzzer PRIVATE ${COWTERM_SANITIZER_FLAGS})
    target_include_directories(CowTermParserFuzzer PRIVATE ${PROJECT_SOURCE_DIR}/Lib)
    target_include_directories(CowTermParserFuzzer PRIVATE
            $<TARGET_PROPERTY:eacp-core,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(CowTermParserFuzzer PRIVATE
            $<TARGET_PROPERTY:eacp-core,INTERFACE_COMPILE_DEFINITIONS>)

    # The kitty key encoder, fed arbitrary key events and protocol flags.
    add_executable(CowTermKeyEncoderFuzzer
            KeyEncoderFuzzer.cpp
            ../Lib/CowTermTerminal/KeyEncoding.cpp
            ../Lib/CowTermTerminal/KittyKeyEncoding.cpp)

    target_compile_options(CowTermKeyEncoderFuzzer PRIVATE ${COWTERM_SANITIZER_FLAGS} -g -O1)
    target_link_options(CowTermKeyEncoderFuzzer PRIVATE ${COWTERM_SANITIZER_FLAGS})
    target_include_directories(CowTermKeyEncoderFuzzer PRIVATE ${PROJECT_SOURCE_DIR}/Lib)
    target_include_directories(CowTermKeyEncoderFuzzer PRIVATE
            $<TARGET_PROPERTY:eacp-graphics,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(CowTermKeyEncoderFuzzer PRIVATE
            $<TARGET_PROPERTY:eacp-graphics,INTERFACE_COMPILE_DEFINITIONS>)
endif ()
