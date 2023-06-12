/*
  reference: https://github.com/Azure/azure-sdk-for-c-arduino/blob/main/examples/Azure_IoT_Hub_ESP32/readme.md
*/

// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

#ifndef SERIALLOGGER_H
#define SERIALLOGGER_H

#include <Arduino.h>

#ifndef SERIAL_LOGGER_BAUD_RATE
#define SERIAL_LOGGER_BAUD_RATE 115200
#endif

class SerialLogger
{
public:
  SerialLogger();
  void Info(String message);
  void Error(String message);
};

extern SerialLogger Logger;

#endif // SERIALLOGGER_H
