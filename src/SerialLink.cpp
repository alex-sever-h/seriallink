/*

SeriaLink 0.1.0

SeriaLink

Copyright (C) 2017 by Xose Pérez <xose dot perez at gmail dot com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <Arduino.h>
#include "SerialLink.h"
#include <rBase64.h>

static char gBuffer[MAX_BUFFER_SIZE];

SerialLink::SerialLink(Stream& serial, bool doACK, int timeout,
                       char splitChar, char queryChar, char terminateChar,
                       char base64Char): _serial(&serial) {
    _doACK = doACK;
    _splitChar = splitChar;
    _base64Char = base64Char;
    _queryChar = queryChar;
    _terminateChar = terminateChar;
    _timeout = timeout;
    _serial->setTimeout(_timeout);
}

int SerialLink::readMessage(byte * buffer) {
  int count;

  count = _serial->available();
  if (count <= 1)
    return 0;

  _serial->setTimeout(_timeout);
  int length = _serial->readBytesUntil(_terminateChar, buffer, MAX_BUFFER_SIZE-1);
  if (length <= 1)
    return 0;

  // Convert it to C string
  buffer[length] = 0;
  return length;
}


void SerialLink::handle() {
  byte buffer[MAX_BUFFER_SIZE];
  int length;

  length = readMessage(buffer);
  if(length == 0)
    return;

  if(strncmp((const char *)&buffer[0], "AT+", 3) != 0) // Not AT comnand
    return;

  // Find the split char
  int split = 0;
  while (char c = buffer[split]) {
    if ((c == _splitChar) || (c == _base64Char)) break;
    ++split;
  }

  // We expect split char and at least on more char after it
  if (length - split <= 1)
    return;

  // key
  char key[split+1];
  memcpy(key, buffer, split);
  key[split] = 0;

  if(buffer[split] == _splitChar){
    // query value
    if (buffer[split+1] == _queryChar) {
      if (_onGet) {
        bool response = onGetDemux(key);
        if (_doACK) {
          if (!response) sendInvalid();
        }
      }
    } else { // set value
      long value = 0;
      while (char c = buffer[++split]) {
        value = 10 * value + c - '0';
      }
      if (_onSet) {
        bool response = onSetDemux(key, value);
        if (_doACK) {
          response ? sendOK() : sendInvalid();
        }
      }
    }
  } else if(buffer[split] == _base64Char){
    int len = rbase64_dec_len((char*)&buffer[split+1], length - (split));
    char base64dec[len];
    len = rbase64_decode(base64dec, (char*)&buffer[split+1], length - (split));
    if (_onSetByteStream) {
      bool response = onSetByteStreamDemux(key, base64dec, len);
      if (_doACK) {
        response ? sendOK() : sendInvalid();
      }
    }
  }
}

bool SerialLink::onGetDemux(char *key) {
  for (char i = 0; i < _onGetNo; i++) {
    bool r = _onGet[i](key);
    if(r) return true;
  }
  return false;
}

bool SerialLink::onSetDemux(char *key, long payload) {
  for (char i = 0; i < _onSetNo; i++) {
    bool r = _onSet[i](key, payload);
    if(r) return true;
  }
  return false;
}

bool SerialLink::onSetByteStreamDemux(char *key, char *payload , size_t payload_size) {
  for (char i = 0; i < _onSetByteStreamNo; i++) {
    bool r = _onSetByteStream[i](key, payload, payload_size);
    if(r) return true;
  }
  return false;
}

void SerialLink::onGet(bool (*callback)(char * command)) {
  if(_onGetNo == NUMBER_CALLBACKS) return;
  _onGet[_onGetNo++] = callback;
}

void SerialLink::onSet(bool (*callback)(char * command, long payload)) {
  if(_onSetNo == NUMBER_CALLBACKS) return;
  _onSet[_onSetNo++] = callback;
}
void SerialLink::onSetByteStream(bool (*callback)(char * command, char * payload, size_t payload_size)) {
  if(_onSetByteStreamNo == NUMBER_CALLBACKS) return;
  _onSetByteStream[_onSetByteStreamNo++] = callback;
}



void SerialLink::clear() {
    _serial->write(_terminateChar);
    _serial->flush();
}

void SerialLink::sendRaw(const char * string) {
    _serial->write(string);
    _serial->write(_terminateChar);
}

void SerialLink::sendRaw_P(const char * string) {
    while (unsigned char c = pgm_read_byte(string++)) _serial->write(c);
    _serial->write(_terminateChar);
}

bool SerialLink::send(const char * command, long payload, bool doACK) {

  char *buffer = gBuffer;
  snprintf(buffer, MAX_BUFFER_SIZE, "%s%c%ld", command, _splitChar, payload);
    sendRaw(buffer);

    bool response = !doACK;
    if (doACK) {

        byte b[3];
        int length = _serial->readBytesUntil(_terminateChar, b, 3);
        if (length == 2 && b[0] == 'O' && b[1] == 'K') response = true;

    }

    return response;

}

bool SerialLink::sendByteStream(const char * command, const char * payload,
                                size_t payload_length, bool doACK) {
  char * buffer = gBuffer;
    int pos = sprintf(buffer, "%s%c", command, _base64Char);
    rbase64_encode(&buffer[pos], (char *)payload, payload_length);

    sendRaw(buffer);

    bool response = !doACK;
    if (doACK) {
        byte b[3];
        int length = _serial->readBytesUntil(_terminateChar, b, 3);
        if (length == 2 && b[0] == 'O' && b[1] == 'K') response = true;
    }

    return response;

}

bool SerialLink::send(const char * command, long payload) {
    return send(command, payload, _doACK);
}


bool SerialLink::send_P(const char * command, long payload, bool doACK) {

    // Find size
    int len = 1;
    char * p = (char *) command;
    while (pgm_read_byte(p++)) len++;

    // Copy string
    char buffer[len];
    strcpy_P(buffer, command);

    return send(buffer, payload, doACK);

}

bool SerialLink::send_P(const char * command, long payload) {
    return send_P(command, payload, _doACK);
}

void SerialLink::query(const char * command) {

    char buffer[strlen(command) + 2];
    sprintf(buffer, "%s%c%c", command, _splitChar, _queryChar);
    sendRaw(buffer);

}

void SerialLink::query_P(const char * command) {

    // Find size
    int len = 1;
    char * p = (char *) command;
    while (pgm_read_byte(p++)) len++;

    // Copy string
    char buffer[len];
    strcpy_P(buffer, command);

    query(buffer);

}

void SerialLink::sendOK() {
    sendRaw_P(PSTR("OK"));
}

void SerialLink::sendInvalid() {
    sendRaw_P(PSTR("INVALID"));
}
