# CMSIS_SVD.hxx/.cxx are generated once by hand (see resources/Schemas and
# engine/INSTALL.md's Regenerating section) and committed - this is not a
# build step. The generated tree and Xerces-C need RTTI and exceptions,
# which the rest of the engine builds without; this target is the isolated
# island where that's true, and it exposes only svd_model.hpp (plain
# structs) so nothing downstream needs to know.

find_package(XercesC REQUIRED)

add_library(device_xml
        generated/CMSIS_SVD.cxx
        src/svd_loader.cc
)

target_include_directories(device_xml PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(device_xml PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/generated
)

target_link_libraries(device_xml PRIVATE
    XercesC::XercesC
)

# PRIVATE: this only needs to affect how device_xml's own sources compile.
# svd_loader's public surface is POD-only (svd_model.hpp), so nothing
# downstream needs RTTI just because it links this target. Exceptions are
# on by default (nothing in this build disables them).
target_compile_options(device_xml PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-frtti>
)

target_compile_features(device_xml PUBLIC cxx_std_23)
