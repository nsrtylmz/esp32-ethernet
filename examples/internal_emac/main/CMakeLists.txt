# "main" automatically depends on all components, so the library is found
# regardless of the folder name it was cloned into.
idf_component_register(SRCS "main.cpp" INCLUDE_DIRS ".")
