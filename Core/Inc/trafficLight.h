#ifndef TRAFFICLIGHT_H
#define TRAFFICLIGHT_H

typedef struct {
	uint8_t redTime;      // 0-23
	uint8_t yellowTime;    // 0-59
	uint8_t greenTime;    // 0-59
	bool redOn;
	bool yellowOn;
	bool greenOn;
	unint16_t redPin;
	unint16_t yellowPin;
	unint16_t greenPin;
	unint8_t redSetTime;
	unint8_t yellowSetTime;
	unint8_t greenSetTime;
} trafficLight;

void turnLightsOnOff(trafficLight *light){
	if (light->redOn){

	}else{

	}
	if (light->yellowOn){

	}else {

	}
	if (light->greenOn){

	}else{

	}
}

void setLightToRedfromGreen(trafficLight *light){
	if (light->greenTime == 0){
		light->redOn = true;
		light->yellowOn = false;
		light->green = false;
		turnLightsOnOff(light);
		resetLightTiming(light);
	}
}

void setLightToYellowFromRed(trafficLight *light){
	if (light->redTime == 0){
			light->redOn = true;
			light->yellowOn = true;
			light->green = false;
			turnLightsOnOff(light);
		}
}
void setLightToGreenFromYellow(trafficLight *light){
	if (light->yellowTime == 0){
		light->redOn = false;
		light->yellowOn = false;
		light->greenOn = true;
		turnLightsOnOff(light);
	}
}

void resetSignalTime(trafficLight *light){
	light->redTime = light.redSetTime;
	light->yellowTime = light.yellowSetTime;
	light->greenTime = light.greenSetTime;

}

void setLightTiming(uint8_t red, uint8_t yellow, uint8_t green, trafficLight *light){
	light->redSetTime = red;
	light->yellowSetTime = yellow;
	light->greenSetTime = green;
}

void setLightPin(uin16_t redPin, uint16_t yellowPin, uint16_t greenPin, trafficLight *light){
	light->redPin = redPin;
	light->yellowPin = yelloPin;
	light->greenPin = greenPin;
}

void signalStart (traffiLight *light){
	resetSignalTime(light);
	light->redOn = true;
	light->yellowOn = false;
	light->greenOn = false;

}

#endif
