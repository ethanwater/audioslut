/*	                 ___            __      __   
	  ____ ___  ______/ (_)___  _____/ /_  __/ /_  
	 / __ `/ / / / __  / / __ \/ ___/ / / / / __/   
	/ /_/ / /_/ / /_/ / / /_/ (__  ) / /_/ / /_     
	\__,_/\__,_/\__,_/_/\____/____/_/\__,_/\__/  */ 

#include <stdio.h>
#include <cstdlib>
#include <math.h>
#include <portaudio.h>
#include <random>

#include "rtmidi/RtMidi.h"

#define SAMPLE_RATE 48000  

/*
 * dev notes:
 * 1. Qt C++20 will be used for the GUI: https://doc.qt.io/qt-6/cpp20-overview.html
 * 2. 
 *
 */
typedef struct {
	int note;
	float volume;
	//PaStream** stream;
} MidiOutput;

float FreqToPhase(float freq);
float MidiToFreq(int midiNote);
float Normalize(float x);
void PrintAudioSlut();
static int AudioStreamCallback(
	const void *inputBuffer,
	void *outputBuffer,
	unsigned long framesPerBuffer,
	const PaStreamCallbackTimeInfo* timeInfo,
	PaStreamCallbackFlags statusFlags,
	void *userData
);
void MidiStreamCallback(
	double deltatime, 
	std::vector<unsigned char> *message, 
	void *userData
);

int main(void) {
	PrintAudioSlut();
	auto error_lambda = [](PaError err, bool critical) { 
		if (err != paNoError) {
			printf("error:%s\n", Pa_GetErrorText(err)); 
			if (critical) {
				std::exit(1);
			};
		}
	};

	/* MIDI */
	//TODO: realtime midi port search 
	PaStream *stream;

	MidiOutput midi = MidiOutput{0, 0.0f};

	std::vector<RtMidi::Api> apis; RtMidi::getCompiledApi(apis);
	RtMidi::Api CoreMidi = apis[0];
	RtMidiIn *midiin = new RtMidiIn(CoreMidi, "Apple CoreMidi", 100); 

	midiin->openPort(0);
	midiin->setCallback(&MidiStreamCallback, &midi);

	/* AUDIO */
	error_lambda(Pa_Initialize(), true);

	std::string portName = midiin->getPortName();
	PaDeviceIndex device_index = Pa_GetDefaultOutputDevice();
	auto *device_info = Pa_GetDeviceInfo(device_index);

	printf("MIDI:   %s\nDEVICE: %s\n\nNOTE TRACE:\n", 
	  portName.c_str(), 
		device_info->name 
	);

	PaStreamParameters pastream_out = PaStreamParameters{
		device_index,
		2,
		paFloat32,
		1,
		NULL
	};

	error_lambda(Pa_OpenStream(
	  &stream,
		NULL, //no input paramaters needed
		&pastream_out,
		SAMPLE_RATE,
		256,
		paNoFlag,
		AudioStreamCallback,
		&midi
	), true);
	error_lambda(Pa_StartStream(stream), true);
	while(true) {};
	error_lambda(Pa_StopStream(stream), false);
	error_lambda(Pa_CloseStream(stream), false);
	error_lambda(Pa_Terminate(), false);
	return 0;
}

//TODO: POLYPHONY:: rtmidi handles this already- so it is solely based on our audio engine
static int AudioStreamCallback
(
	const void *inputBuffer,
	void *outputBuffer,
	unsigned long framesPerBuffer,
	const PaStreamCallbackTimeInfo* timeInfo,
	PaStreamCallbackFlags statusFlags,
	void *userData
) {
	float *out = (float*)outputBuffer;
	MidiOutput *data = (MidiOutput*)userData;
	(void) inputBuffer;

	float note_frequency = MidiToFreq(data->note);
	float note_phase = FreqToPhase(note_frequency);
	float note_phase_iter = note_phase/SAMPLE_RATE;
	float note_volume = data->volume;
	static float ctr = 0.0;
	
	for(unsigned int i =0; i <framesPerBuffer; i++) {
		if (data->note != 0) {
			ctr += note_phase_iter;
			if (ctr > (2.0f*M_PI)) {
				ctr = fmod(ctr, (2.0f*M_PI)) * 0.1;
			}
		}
		*out++ = sin(ctr);
		*out++ = sin(ctr);
	}

  return paContinue;
}

void MidiStreamCallback(double deltatime, std::vector<unsigned char> *message, void *userData) {
	unsigned int nbytes = message->size();
	int statusbyte = message->at(0);
	int notebyte = message->at(1);
	int volumebyte = message->at(2);

	MidiOutput *data = (MidiOutput*)userData;

	//DEBUGGING PURPOSES
	bool is_note_on = (statusbyte == 144) ? true : false;
  if (!is_note_on) {
		data->note = 0;
		data->volume = 0.0f;
		std::cout<<"[NOTE OFF]"<< std::endl;
	} else {
		data->note = notebyte;
		data->volume = (float)volumebyte;
		std::cout<<"[NOTE ON ]  Note:: " << data->note << " Volume:: " << data->volume;
		for ( unsigned int i=0; i<nbytes; i++ )
  	  std::cout << "Byte " << i << " = " << (int)message->at(i) << ", ";
  	if ( nbytes > 0 )
  	  std::cout << "stamp = " << deltatime << std::endl;
	};
};

float FreqToPhase(float freq) {
	float phase = ((2*M_PI)*freq);
	return phase;
}

float MidiToFreq(int midiNote) {
	float freq = 440.0 * pow(2., ((float)midiNote-69.)/12.);
	return freq;
}

float Normalize(float x) {
	double normal = 1.0 / sqrt((double)x);
	return (float)normal;
}

void PrintAudioSlut() {
	std::cout << R"(
  	                   ___            __      __   
	  ____ ___  ______/ (_)___  _____/ /_  __/ /_  
	 / __ `/ / / / __  / / __ \/ ___/ / / / / __/   
	/ /_/ / /_/ / /_/ / / /_/ (__  ) / /_/ / /_     
	\__,_/\__,_/\__,_/_/\____/____/_/\__,_/\__/  )" << std::endl << std::endl;
}
