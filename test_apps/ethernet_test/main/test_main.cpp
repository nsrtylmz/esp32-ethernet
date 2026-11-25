/*
 * Unit / integration tests for the Ethernet library.
 *
 *  - Tests tagged [api] need no Ethernet hardware and run automatically at boot.
 *  - Tests tagged [hw] need a W5500 module (pins: menuconfig > "Ethernet Test Configuration")
 *    and an Ethernet cable connected to a network with a DHCP server.
 *    They can be started from the interactive Unity menu that opens afterwards
 *    (type  [hw]  and press Enter, or the test number).
 */

#include "unity.h"
#include "unity_test_runner.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main(void)
{
	vTaskDelay(pdMS_TO_TICKS(500));   // let the serial monitor attach

	UNITY_BEGIN();
	unity_run_tests_by_tag("[hw]", true);   // everything except hardware tests
	UNITY_END();

	unity_run_menu();
}
