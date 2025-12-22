#include <stdio.h>
#include <cstdlib>
#include <math.h>
#include <portaudio.h>
#include "rtmidi/RtMidi.h"
#include <random>

#define SAMPLE_RATE 48000

//typedef struct
//{
//	float left_phase;
//	float right_phase;
//} 
//phaseData;

float nox = 0.0f; 

static int AudioStreamCallback
(
	const void *inputBuffer,
	void *outputBuffer,
	unsigned long framesPerBuffer,
	const PaStreamCallbackTimeInfo* timeInfo,
	PaStreamCallbackFlags statusFlags,
	void *userData
) {
	//phaseData *data = (phaseData*)userData;
	float *out = (float*)outputBuffer;
	unsigned int i;
	(void) inputBuffer;
	
	for (i = 0; i < framesPerBuffer; i++) {
		//TODO: make it not sound like grinding teeth
    float x = sinf(nox);
    *out = x;
	}
  return paContinue;
}

float midiToFrequency(int midiNoteNumber);

void MidiStreamCallback(double deltatime, std::vector<unsigned char> *message, void *userData) {
	//byte0 -> statusbyte
	//byte1 -> note (pitch)
	//byte2 -> attack velocity (volume)
	if ((int)message->at(2) == 0) {
		nox = 0.0f;
	} else {
		int midinote = message->at(1); //byte1 is all we care about rn
		float freq = midiToFrequency(midinote);
		nox = freq;
		printf("note: %u | freq: %f\n", midinote, freq);
	}
};

float midiToFrequency(int midiNoteNumber) {
	float freq = 440.0 * pow(2., ((float)midiNoteNumber-69.)/12.);
	return freq;
}

int main(void) {
	/* midi */
	//TODO: support layering notes
	std::vector<RtMidi::Api> apis;
  RtMidi::getCompiledApi(apis);
	RtMidi::Api CoreMidi = apis[0];

	RtMidiIn *midiin = new RtMidiIn(CoreMidi, "Apple CoreMidi Hook", 100); 

	unsigned int nPorts = midiin->getPortCount();
  if ( nPorts == 0 ) {
    std::cout << "No ports available!\n";
  } else {
		printf("num of midi ports: %u\n", nPorts);
	}

	midiin->openPort(0);
	midiin->setCallback(&MidiStreamCallback);


	/* sound */
	auto error_lambda = [](PaError err, bool critical) { 
		if (err != paNoError) {
			printf("error:%s\n", Pa_GetErrorText(err)); 
			if (critical) {
				std::exit(1);
			};
		}
	};

	PaError err = Pa_Initialize();
	error_lambda(err, true);

	PaStream *stream;
	//phaseData data = {0.0f, 0.0f};
	int* data = nullptr;
	
	auto device_idx = Pa_GetDefaultOutputDevice();
	auto device_ct = Pa_GetDeviceCount();
	printf("**available devices: %d\n", device_ct);

	auto *info = Pa_GetDeviceInfo(device_idx);
	printf("**device: %s\n**max-output-channels -> %d\n", 
	  info->name, info->maxOutputChannels
	);
	
	PaStreamParameters pastream_out = PaStreamParameters{
		device_idx,
		2,
		paFloat32,
		1
	};

	PaError openStreamResult = Pa_OpenStream(
	  &stream,
		NULL,
		&pastream_out,
		SAMPLE_RATE,
		256,
		paNoFlag,
		AudioStreamCallback,
		&data
	);
	error_lambda(openStreamResult, false);

	PaError start_playback = Pa_StartStream(stream);
	error_lambda(start_playback, true);
	
	while(true) {};

	PaError stop_playback = Pa_StopStream(stream);
	error_lambda(stop_playback, false);

	error_lambda(Pa_CloseStream(stream), false);

	error_lambda(Pa_Terminate(), false);
	return 0;
}


