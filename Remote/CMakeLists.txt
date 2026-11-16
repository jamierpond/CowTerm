# The headless ct a remote machine installs; ssh carries the session daemon's
# messages through its `ct bridge`. Everything it runs is in CowTerm::Multiplexer.
add_executable(CowTermCli CtMain.cpp)
target_link_libraries(CowTermCli PRIVATE CowTerm::Multiplexer CowTermVersion)
set_target_properties(CowTermCli PROPERTIES OUTPUT_NAME ct)
set_default_target_setting(CowTermCli)
