#pragma once
#include <Arduino.h>
[[noreturn]] void portalRun();   // provisioning mode: AP + captive portal, until a successful save reboots or the timeout sleeps
