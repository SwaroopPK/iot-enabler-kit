
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <system/platform.h>
#include <NVSettings.h>
#include <system/Switch.h>
#include <drv/Glyph/lcd.h>
#include <mstimer.h>
#include <system/console.h>
#include <jsmn/jsmn.h>
#include "r_cg_timer.h"
#include <sensors/Accelerometer.h>
#include <sensors/LightSensor.h>
#include <sensors/Temperature.h>
#include <sensors/Potentiometer.h>
#include "Apps.h"
#include "led.h"
#include <drv/EInk/eink_driver.h>
#include <drv/EInk/user_app.h>
#include "App_Dweet.h"

#define UPDATE_PERIOD 5000
#define NUM_SENSOR 5
#define CAPABILITIES_PERIOD 10000
#define MAX_PROD_ERRORS 5

#define CLIENT_VER	"Dweet Client R1.1.1"
/*-------------------------------------------------------------------------*
* Constants:
*-------------------------------------------------------------------------*/
uint16_t period = NUM_SENSOR;

/*  Default dweet connection parameters  */
#define DWEET_HOST "dweet.io"
#define DWEET_PORT 80
#define THING_ID_EEPROM_LOC 128
#define JSON_TOKEN_MAX 100

const char true_val[] = "true";
const char false_val[] = "false";

char	animation[] = " -\0 \\\0 |\0 /\0";
char    thingID[32];
char	lastUpdate[32];

uint16_t mic_level;
uint32_t tone_stop;


jsmn_parser parser;
jsmntok_t tokens[JSON_TOKEN_MAX];
jsmnerr_t jr;

char 	msg[400];
char    ledmsg[100];

bool    connected;

/*----------------------------------------------------------------------------*
*	Routine: App_DweetConnector
*----------------------------------------------------------------------------*
*	Description:
*		Run the Dweet connector demo indefinitely
*----------------------------------------------------------------------------*/

void App_DweetConnector(void)
{
	thingID[0] = NULL;
	lastUpdate[0] = NULL;
	msg[0] = NULL;
        ledmsg[0] = NULL;
	uint8_t cid = ATLIBGS_INVALID_CID;
        uint16_t value;

	// If switch 3 is pressed, reset the EEPROM
	if(Switch3IsPressed())
	{
		EEPROM_Write(THING_ID_EEPROM_LOC, "\0", 1);
	}
	else
	{
		// Do we have a thingID saved to our EEPROM?
		EEPROM_Read(THING_ID_EEPROM_LOC, msg, sizeof(thingID));

		if(msg[0] == '!')
		{
			memcpy(thingID, msg + 1, sizeof(thingID));
			msg[0] = NULL;
		}
	}

	ATLIBGS_MSG_ID_E r;

	initEink();
	setLogo(11);

	R_TAU0_Create();

	App_InitModule();

	DisplayLCD(LCD_LINE1, "");
	DisplayLCD(LCD_LINE2, "dweet.io");
	DisplayLCD(LCD_LINE3, "");

	if(thingID[0] == NULL)
	{
		DisplayLCD(LCD_LINE4, "");
	}
	else
	{
		DisplayLCD(LCD_LINE5, "thing:");
		DisplayLCD(LCD_LINE6, thingID);
	}

	DisplayLCD(LCD_LINE7, "");
	DisplayLCD(LCD_LINE8, "connecting.......");

	App_aClientConnection();		//Will block until connected
	AtLibGs_SetNodeAssociationFlag();
	memset(msg, '\0', sizeof(msg));
        memset(ledmsg, '\0', sizeof(ledmsg));


	led_all_off();

	Temperature_Init();
	Potentiometer_Init();
	Accelerometer_Init();

	MSTimerDelay(100);
	connected = false;

	char* animationIndex = animation;

	while(1) {
		if (!AtLibGs_IsNodeAssociated()) {
			DisplayLCD(LCD_LINE8, "connecting.......");
			App_InitModule();
                        App_aClientConnection();
			AtLibGs_SetNodeAssociationFlag();
		}

		connected = true;
		while (connected)
		{
			DisplayLCD(LCD_LINE1, (const uint8_t *) CLIENT_VER);
                        DisplayLCD(LCD_LINE2, "visit:");
			DisplayLCD(LCD_LINE3, "dweet.io/follow");

			DisplayLCD(LCD_LINE8, animationIndex);

			animationIndex += strlen(animationIndex) + 1;
			if(animationIndex >= animation + sizeof(animation) - 1)
			{
				animationIndex = animation;
			}


			r = dweetData(&cid);
			if (r != ATLIBGS_MSG_ID_OK)
			{
				connected = false;
			}
                        if (MSTimerGet() > tone_stop) {
				R_TAU0_Channel0_Stop();
			}

			MSTimerDelay(500);

			r = checkData(&cid);
			if (r != ATLIBGS_MSG_ID_OK)
			{
				connected = false;
			}

                        value = Microphone_Get();
                        value = Microphone_Get();
                        value = abs((int)(value/4)-493);
                        if (value > mic_level){
                              mic_level = value;
                        }

			MSTimerDelay(500);
		}

		AtLibGs_CloseAll();
		cid = ATLIBGS_INVALID_CID;
                AtLibGs_ClearNodeAssociationFlag();
		DisplayLCD(LCD_LINE8, "connecting.......");
		MSTimerDelay(5000);		//TODO - exponential backoff
	}
}

void addJSONKeyNumberValue(char* jsonString, const char* keyName, float value)
{
	int pos = strlen(jsonString);

	if(jsonString[pos - 1] != '{')
	{
		strcat(jsonString, ",");
		pos++;
	}

	sprintf(jsonString + pos, "\"%s\":%.2f", keyName, value);
}

void addJSONKeyStringValue(char* jsonString, const char* keyName, const char* value)
{
	int pos = strlen(jsonString);

	if(jsonString[pos - 1] != '{')
	{
		strcat(jsonString, ",");
		pos++;
	}

	sprintf(jsonString + pos, "\"%s\":\"%s\"", keyName, value);
}

void addJSONKeyBooleanValue(char* jsonString, const char* keyName, const char* value)
{
	int pos = strlen(jsonString);

	if(jsonString[pos - 1] != '{')
	{
		strcat(jsonString, ",");
		pos++;
	}

	sprintf(jsonString + pos, "\"%s\":%s", keyName, value);
}


const char * ledValue(int idx) {
	if (led_get(idx) == 1) {
		return true_val;
	} else {
		return false_val;
	}
}

ATLIBGS_MSG_ID_E checkData(uint8_t* cid)
{
	if(thingID[0] == NULL)
	{
		return ATLIBGS_MSG_ID_OK;
	}

	ATLIBGS_MSG_ID_E r;

	char thingPath[64];
	sprintf(thingPath, "/get/latest/dweet/for/%s-send", thingID);
	strcpy(msg, "\r\n");

        int val;

	r = httpRequest(ATLIBGS_HTTPSEND_GET, 5000, thingPath, msg, cid);
	if (r != ATLIBGS_MSG_ID_OK)
	{
		return r;
        }

	char* body = strstr(msg, "{");

	jsmn_init(&parser);

	jr = jsmn_parse(&parser, body, tokens, JSON_TOKEN_MAX);
	if (jr != JSMN_SUCCESS)
	{
		return ATLIBGS_MSG_ID_OK;
	}

	if(getValue(body, tokens, JSON_TOKEN_MAX, "this", msg) && strcmp(msg, "succeeded") == 0)
	{
		if(getValue(body, tokens, JSON_TOKEN_MAX, "created", msg) && strcmp(msg, lastUpdate) != 0)
		{
			strcpy(lastUpdate, msg); // Hold on to the update time so we don't re-use this

			if(getValue(body, tokens, JSON_TOKEN_MAX, "lcd_text", msg))
			{
				DisplayLCD(LCD_LINE7, msg);
			}

			if(getValue(body, tokens, JSON_TOKEN_MAX, "beep", msg))
			{
                                if (msg[0] == 't') {
                                        ConsolePrintf("Beeping");
                                        R_TAU0_Channel0_Freq(1000);
                                        tone_stop = MSTimerGet()+2000;
                                        R_TAU0_Channel0_Start();
                                        // MSTimerDelay(2000);
                                        //R_TAU0_Channel0_Stop();
                                }
			}


                        int ledUpdate = findKey(body, tokens, JSON_TOKEN_MAX, "led");
                        /*
                        int start = tokens[ledUpdate].start;
                        ConsolePrintf("%d",start);
                        int end = tokens[ledUpdate].end;
                        ConsolePrintf("%d",end);*/
                        if(ledUpdate >= 0)
                        {
                                for (int i=0;i<256;i++){
                                      if (tokens[i].type == JSMN_STRING) {
                                          /*if ((tokens[i].start < start)||(tokens[i].end > end))
                                            continue;*/
                                          if (strncmp(body+tokens[i].start, "led", 3) == 0) {
                                              val = atoi(body+tokens[i].start+3);
                                              if (body[tokens[i+1].start] == 't') {
                                                  ConsolePrintf("LED %d ON\n", val);
                                                  led_on(val-3);
                                              } else {
                                                    ConsolePrintf("LED %d OFF\n", val);
                                                    led_off(val-3);
                                              }
                                          }
                                      }
                                }
                        }
		}
	}

	return ATLIBGS_MSG_ID_OK;
}

ATLIBGS_MSG_ID_E dweetData(uint8_t* cid)
{
	ATLIBGS_MSG_ID_E r;

	uint16_t iValue;
	float fValue;
	extern int16_t gAccData[3];

	msg[0] = '{';
	msg[1] = '\0';

        ledmsg[0] = '{';
        ledmsg[1] = '\0';

	// Get our temp
	iValue = Temperature_Get();
	fValue = (((float)iValue) / 128.0) - 2.5;
        fValue = (fValue * 1.8) + 32.0;

	addJSONKeyNumberValue(msg, "Temperature", fValue);

	// Get our light
	iValue = LightSensor_Get();
	addJSONKeyNumberValue(msg, "Light", iValue);

	// Get our accelerometer
	Accelerometer_Get();
	addJSONKeyNumberValue(msg, "Tilt_X", gAccData[0] / 33.0 * 90);
	addJSONKeyNumberValue(msg, "Tilt_Y", gAccData[1] / 33.0 * 90);
	addJSONKeyNumberValue(msg, "Tilt_Z", gAccData[2] / 30.0 * 90);

	// Get our Buttons
	addJSONKeyNumberValue(msg, "Button_1", Switch1IsPressed());
	addJSONKeyNumberValue(msg, "Button_2", Switch2IsPressed());
	addJSONKeyNumberValue(msg, "Button_3", Switch3IsPressed());

        // Get Microphone level
	addJSONKeyNumberValue(msg, "Microphone", mic_level);
        mic_level = 0;

	// Get our pot
        Potentiometer_Get();
	addJSONKeyNumberValue(msg, "Potentiometer", Potentiometer_Get());


        // Get our LED statuses
        addJSONKeyBooleanValue(ledmsg, "led3", ledValue(0));
        addJSONKeyBooleanValue(ledmsg, "led4", ledValue(1));
        addJSONKeyBooleanValue(ledmsg, "led5", ledValue(2));
        addJSONKeyBooleanValue(ledmsg, "led6", ledValue(3));
        addJSONKeyBooleanValue(ledmsg, "led7", ledValue(4));
        addJSONKeyBooleanValue(ledmsg, "led8", ledValue(5));
        addJSONKeyBooleanValue(ledmsg, "led9", ledValue(6));
        addJSONKeyBooleanValue(ledmsg, "led10", ledValue(7));
        addJSONKeyBooleanValue(ledmsg, "led11", ledValue(8));
        addJSONKeyBooleanValue(ledmsg, "led12", ledValue(9));
        addJSONKeyBooleanValue(ledmsg, "led13", ledValue(10));
        addJSONKeyBooleanValue(ledmsg, "led14", ledValue(11));
        addJSONKeyBooleanValue(ledmsg, "led15", ledValue(12));
        strcat(ledmsg, "}\r\n");
        strcat(msg, ",\"LED\":");
        strcat(msg,ledmsg);
        
        // Include client version
        addJSONKeyStringValue(msg, "Version", (const uint8_t *) CLIENT_VER);
        
	strcat(msg, "}\r\n");

	char thingPath[32];

	if(thingID[0] != NULL)
	{
		sprintf(thingPath, "/dweet/for/%s", thingID);
	}
	else
	{
		strcpy(thingPath, "/dweet");
	}

	r = httpRequest(ATLIBGS_HTTPSEND_POST, 5000, thingPath, msg, cid);
	if (r != ATLIBGS_MSG_ID_OK)
	{
		return r;
        }

	// If we don't have a thing. Get it.
	if(thingID[0] == NULL)
	{
		char* body = strstr(msg, "{");
                int l = strlen(body);
             //   char* body = strstr(body1, "{");
		jsmn_init(&parser);
                //ConsolePrintf("Body: %d, %s", l, body);
		jr = jsmn_parse(&parser, body, tokens, JSON_TOKEN_MAX);

		if(getValue(body, tokens, JSON_TOKEN_MAX, "thing", thingID))
		{
			DisplayLCD(LCD_LINE4, "thing:");
			DisplayLCD(LCD_LINE5, thingID);

			// Save it to EEPROM
			EEPROM_Write(THING_ID_EEPROM_LOC, "!", 1);
			EEPROM_Write(THING_ID_EEPROM_LOC + 1, thingID, strlen(thingID) + 1);
		}
	}

	return ATLIBGS_MSG_ID_OK;
}

ATLIBGS_MSG_ID_E httpRequest(ATLIBGS_HTTPSEND_E type, uint16_t timeout, char* page, char* inOutBuffer, uint8_t* cid)
{
	ATLIBGS_MSG_ID_E r;

	App_PrepareIncomingData();

	if(*cid == ATLIBGS_INVALID_CID)
	{
	  r = AtLibGs_HTTPOpen(DWEET_HOST, DWEET_PORT, false, NULL, NULL, timeout, cid);
	  if(r != ATLIBGS_MSG_ID_OK || *cid == ATLIBGS_INVALID_CID)
	  {
		  return r;
	  }
	}

	AtLibGs_HTTPConf(ATLIBGS_HTTP_HE_HOST, DWEET_HOST);

	int contentLength = strlen(inOutBuffer);
	char contentLengthStr[4];
	sprintf(contentLengthStr, "%d", contentLength);

	if(type == ATLIBGS_HTTPSEND_POST)
	{
		AtLibGs_HTTPConf(ATLIBGS_HTTP_HE_CON_TYPE, "application/json");
	}

	AtLibGs_HTTPConf(ATLIBGS_HTTP_HE_CON_LENGTH, contentLengthStr);
	//AtLibGs_HTTPConf(ATLIBGS_HTTP_HE_CONN, "close");

	r = AtLibGs_HTTPSend(*cid, type, timeout, page, contentLength, inOutBuffer);

	if(r != ATLIBGS_MSG_ID_OK)
	{
		return r;
	}

	r = AtLibGs_WaitForHTTPMessage(timeout);
	if(r != ATLIBGS_MSG_ID_HTTP_RESPONSE_DATA_RX)
	{
		return r;
	}

	//AtLibGs_HTTPClose(*cid);

	ATLIBGS_HTTPMessage msg;
	AtLibGs_ParseHTTPData(G_received, G_receivedCount, &msg);

	memcpy(inOutBuffer, msg.message, msg.numBytes);
	inOutBuffer[msg.numBytes] = NULL; // Terminate the string


	return ATLIBGS_MSG_ID_OK;
}

int getValue(char * jsonpos, jsmntok_t * tokens, int toklen, const char * key, char* outValue)
{
	outValue[0] = NULL;

	int tokenIndex = findKey(jsonpos, tokens, toklen, key);

	if (tokenIndex < 0)
	{
		return 0;
	}

	int tokenLen = tokens[tokenIndex+1].end - tokens[tokenIndex+1].start;
	memcpy(outValue, jsonpos + tokens[tokenIndex+1].start, tokenLen);
	outValue[tokenLen] = NULL;  // Add our string term character

	return 1;
}

int findKey(char * jsonpos, jsmntok_t * tokens, int toklen, const char * key) {
	int ret = -1;
	for (int i=0;i<toklen;i++){
		if (tokens[i].type == JSMN_STRING) {
			if (strncmp(jsonpos+tokens[i].start, key, strlen(key)) == 0) {
				return i;
			}
		}
	}
	return ret;
}

