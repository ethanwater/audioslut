#include <stdio.h>
#include <cstdlib>
#include <math.h>
#include <portaudio.h>
#include "rtmidi/RtMidi.h"
#include <random>

#define SAMPLE_RATE 48000

typedef struct
{
	float left_phase;
	float right_phase;
} 
phaseData;

static int AudioStreamCallback
(
	const void *inputBuffer,
	void *outputBuffer,
	unsigned long framesPerBuffer,
	const PaStreamCallbackTimeInfo* timeInfo,
	PaStreamCallbackFlags statusFlags,
	void *userData
) {
	phaseData *data = (phaseData*)userData;
	float *out = (float*)outputBuffer;
	unsigned int i;
	(void) inputBuffer;
	
	for (i = 0; i < framesPerBuffer; i++) {
    float left = sinf(data->left_phase);
    float right = sinf(data->right_phase);

    *out++ = left;
    *out++ = right;

    data->left_phase += 2.0f * M_PI * 820.0f / SAMPLE_RATE;
    data->right_phase += 2.0f * M_PI * 820.0f / SAMPLE_RATE;

    if (data->left_phase >= 2.0f * M_PI)
        data->left_phase -= 2.0f * M_PI;
    if (data->right_phase >= 2.0f * M_PI)
        data->right_phase -= 2.0f * M_PI;
	}
  return paContinue;
}

void MidiStreamCallback(double deltatime, std::vector<unsigned char> *message, void *userData) {
	unsigned int nBytes = message->size();
  for (unsigned int i=0; i<nBytes; i++) {
		//byte0 -> statusbyte
		//byte1 -> note (pitch)
		//byte2 -> attack velocity (volume)
    std::cout << "Byte " << i << " = " << (int)message->at(i) << ", ";
	}
  if (nBytes > 0) {
    std::cout << "stamp = " << deltatime << std::endl;
	}
};

int main(void) {
	/* midi */
	std::vector<RtMidi::Api> apis;
  RtMidi::getCompiledApi(apis);
	RtMidi::Api coreapi = apis[0];

	RtMidiIn *midiin = new RtMidiIn(coreapi, "Apple CoreMidi Hook", 100); 

	unsigned int nPorts = midiin->getPortCount();
  if ( nPorts == 0 ) {
    std::cout << "No ports available!\n";
  } else {
		printf("num of midi ports: %u\n", nPorts);
	}


	midiin->openPort(0);
	midiin->setCallback(&MidiStreamCallback);

	while(true) {};


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
	phaseData data = {0.0f, 0.0f};
	
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

	Pa_Sleep(2000);

	PaError stop_playback = Pa_StopStream(stream);
	error_lambda(stop_playback, false);

	error_lambda(Pa_CloseStream(stream), false);

	error_lambda(Pa_Terminate(), false);
	return 0;
}


