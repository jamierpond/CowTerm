# Writes Snapshots.txt: the screen each corpus recording replays to, which
# Tests/FuzzCorpusReplayTests.cpp checks. Run by scripts/update-fuzz-snapshots.sh.
add_executable(CowTermWriteFuzzSnapshots WriteSnapshots.cpp)
target_link_libraries(CowTermWriteFuzzSnapshots PRIVATE CowTerm::Emulator)

# libFuzzer target for TermParser::feed and the screen it drives, under
# AddressSanitizer and UndefinedBehaviorSanitizer. Needs a Clang that ships
# libFuzzer (Apple's does not); scripts/run-parser-fuzzer.sh configures it.
option(COWTERM_ENABLE_FUZZING "Build the TermParser libFuzzer target" OFF)

if (COWTERM_ENABLE_FUZZING)
    set(COWTERM_SANITIZER_FLAGS -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all)

    add_executable(CowTermParserFuzzer
            ParserFuzzer.cpp
            ../Lib/Emulator/CharWidth.cpp
            ../Lib/Emulator/GraphemeBreak.cpp
            ../Lib/Emulator/CsiParameters.cpp
            ../Lib/Emulator/HyperlinkTable.cpp
            ../Lib/Emulator/KittyKeyboardFlags.cpp
            ../Lib/Emulator/Reflow.cpp
            ../Lib/Emulator/SelectGraphicRendition.cpp
            ../Lib/Emulator/TermParser.cpp
            ../Lib/Emulator/TermScreen.cpp)

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
            ../Lib/TerminalView/KeyEncoding.cpp
            ../Lib/TerminalView/KittyKeyEncoding.cpp)

    target_compile_options(CowTermKeyEncoderFuzzer PRIVATE ${COWTERM_SANITIZER_FLAGS} -g -O1)
    target_link_options(CowTermKeyEncoderFuzzer PRIVATE ${COWTERM_SANITIZER_FLAGS})
    target_include_directories(CowTermKeyEncoderFuzzer PRIVATE ${PROJECT_SOURCE_DIR}/Lib)
    target_include_directories(CowTermKeyEncoderFuzzer PRIVATE
            $<TARGET_PROPERTY:eacp-graphics,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(CowTermKeyEncoderFuzzer PRIVATE
            $<TARGET_PROPERTY:eacp-graphics,INTERFACE_COMPILE_DEFINITIONS>)
endif ()
