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
#include <Base64.h>

SerialLink::SerialLink(Stream& serial, bool doACK, char splitChar, char queryChar, char terminateChar, char base64Char): _serial(&serial) {
    _doACK = doACK;
    _splitChar = splitChar;
    _base64Char = base64Char;
    _queryChar = queryChar;
    _terminateChar = terminateChar;
    _serial->setTimeout(100);
}

void SerialLink::handle() {

    int count = _serial->available();
    if (count>1) {

        byte buffer[MAX_BUFFER_SIZE];
        int length = _serial->readBytesUntil(_terminateChar, buffer, MAX_BUFFER_SIZE-1);

        if (length>1) {

            // Convert it to C string
            buffer[length] = 0;

            // Find the split char
            int split = 0;
            while (char c = buffer[split]) {
              if ((c == _splitChar) || (c == _base64Char)) break;
                ++split;
            }

            // We expect split char and at least on more char after it
            if (length - split > 1) {

                // key
                char key[split+1];
                memcpy(key, buffer, split);
                key[split] = 0;

                if(buffer[split] == _splitChar){
                  // query value
                  if (buffer[split+1] == _queryChar) {

                    if (_onGet) {
                      bool response = _onGet(key);
                      if (_doACK) {
                        if (!response) sendInvalid();
                      }
                    }

                    // set value
                  } else {

                    long value = 0;
                    while (char c = buffer[++split]) {
                      value = 10 * value + c - '0';
                    }
                    if (_onSet) {
                      bool response = _onSet(key, value);
                      if (_doACK) {
                        response ? sendOK() : sendInvalid();
                      }
                    }
                  }
                } else if(buffer[split] == _base64Char){
                  char * base64dec[MAX_BUFFER_SIZE];
                  base64_decode(base64dec, &buffer[split+1], length - (split-1));
                  if (_onSetByteStream) {
                    bool response = _onSetByteStream(key, base64dec, (length - (split-1)) * 3 / 4);
                    if (_doACK) {
                      response ? sendOK() : sendInvalid();
                    }
                  }
                }
            }
        }
    }
}

void SerialLink::onGet(bool (*callback)(char * command)) {
    _onGet = callback;
}

void SerialLink::onSet(bool (*callback)(char * command, long payload)) {
    _onSet = callback;
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

    char buffer[strlen(command) + 10];
    sprintf(buffer, "%s%c%ld", command, _splitChar, payload);
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

    char buffer[MAX_BUFFER_SIZE];
    int pos = sprintf(buffer, "%s%c", command, _base64Char);
    base64_encode(&buffer[pos], (char *)payload, payload_length);

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
