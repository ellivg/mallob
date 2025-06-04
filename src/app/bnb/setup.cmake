
# Add compilation units of the application (only the reader code)
set(BNB_SOURCES src/app/bnb/bnb_job.cpp src/app/bnb/bnb_reader.cpp)

# Add these sources to Mallob's base sources
set(MALLOB_COREPLUSCOMM_SOURCES ${MALLOB_COREPLUSCOMM_SOURCES} ${BNB_SOURCES} CACHE INTERNAL "")

#message("commons+BNB sources: ${BASE_SOURCES}") # Use to debug

# Done!
