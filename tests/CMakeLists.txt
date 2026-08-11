find_package(Qt6 REQUIRED COMPONENTS Test)
include(ECMAddTests)

ecm_add_test(test_devicelistmodel.cpp
    TEST_NAME test_devicelistmodel
    LINK_LIBRARIES multibtaudiocore Qt6::Test
)

ecm_add_test(test_combinesinkmanager.cpp
    TEST_NAME test_combinesinkmanager
    LINK_LIBRARIES multibtaudiocore Qt6::Test
)
