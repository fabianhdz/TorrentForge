# Third-party components

- `nlohmann/json.hpp`: JSON for Modern C++ 3.11.2, an unmodified single-header
  distribution. Used to represent decoded strings and serialize JSON output.
  Upstream: <https://github.com/nlohmann/json/tree/v3.11.2>.
  License: [MIT](nlohmann/LICENSE.MIT).

The JSON header is vendored so configuring and compiling require no downloads
or system JSON package. CMake marks this include directory as a system include
path to separate dependency diagnostics from application warnings.
