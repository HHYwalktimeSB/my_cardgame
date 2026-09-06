cmake_minimum_required(VERSION 3.5)
project(card_game_test CXX)

add_executable(${PROJECT_NAME}
    test_main.cc
    ../controllers/BattleEngine.cc)

target_include_directories(${PROJECT_NAME}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../controllers)

# ##############################################################################
# If you include the drogon source code locally in your project, use this method
# to add drogon 
# target_link_libraries(${PROJECT_NAME} PRIVATE drogon)
#
# and comment out the following lines
target_link_libraries(${PROJECT_NAME} PRIVATE Drogon::Drogon)

ParseAndAddDrogonTests(${PROJECT_NAME})
