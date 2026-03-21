#include "AbstractReceiver.h"

AS::AbstractReceiver::AbstractReceiver(AS::World* world,Model* model) : 
	mWorld(world),
	mModel(model)
{
}

void AS::AbstractReceiver::update()
{
	for(int i = 1; i < static_cast<int>(RADIOS); i++)
	{
		updateRadio(i);
	}
}

float AS::AbstractReceiver::adjustHeading(float degrees)
{
	float result = degrees;
	while(result < 0) result += 360;
	while(result > 359) result -= 360;
	return result;
}

float AS::AbstractReceiver::adjustDeviation(float degrees)
{
	float result = degrees;
	while(result < -180) result += 360;
	while(result > 180) result -= 360;
	return result;
}

bool AS::AbstractReceiver::isLocalizer(int frequency)
{
	if(frequency < 10800 || frequency > 11195) return false;
	return (fmod(static_cast<float>(frequency) / 10.0f,2) != 0); // second last decimal should be odd
};