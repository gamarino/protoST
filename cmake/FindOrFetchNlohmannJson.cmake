# nlohmann/json via FetchContent. Mirrors FindOrFetchCatch2.cmake so the
# offline FetchContent cache (FETCHCONTENT_BASE_DIR) is reused consistently.
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)
