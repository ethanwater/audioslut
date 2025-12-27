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
#include <chrono>
#include <thread>

#include "rtmidi/RtMidi.h"

#define SAMPLE_RATE 48000  

/*
 * dev notes:
 * Qt C++20 will be used for the GUI: https://doc.qt.io/qt-6/cpp20-overview.html
 * SUBTRACTIVE SYNTHESIS: https://en.wikipedia.org/wiki/Subtractive_synthesis
 * VST/VST3 Compatability is a fuckin MUST. What would be the point without it? 
 */

bool isNoteBeingPlayed;

typedef struct {
	int note;
	float volume;
	//PaStream** stream;
} MidiOutput;

float FreqToPhase(float freq);
float MidiToFreq(int midiNote);
float NotePhase(int note);
float Normalize(float x);
void PrintAudioSlut();

static int ArpeggioAudioStreamCallback(
	const void *inputBuffer,
	void *outputBuffer,
	unsigned long framesPerBuffer,
	const PaStreamCallbackTimeInfo* timeInfo,
	PaStreamCallbackFlags statusFlags,
	void *userData
);

static int SimpleAudioStreamCallback(
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

//TODO: MIDI SOURCE POLLING FOR NEAR REALTIME DEV. CONNECTIONS
//bool PollMidiSource(RtMidiIn *midi) {
//	//sleep 1sec
//	//get port count
//	//if no ports found return true
//	std::this_thread::sleep_for(std::chrono::seconds(1));
//	unsigned int pc = midi->getPortCount();
//	return (pc > 0);
//}

int main(void) {
	isNoteBeingPlayed = false;
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
	PaStream *stream;

	MidiOutput midi = MidiOutput{0, 0.0f};

	std::vector<RtMidi::Api> apis; RtMidi::getCompiledApi(apis);
	RtMidi::Api CoreMidi = apis[0];
	RtMidiIn *midiin = new RtMidiIn(CoreMidi, "Apple CoreMidi", 100); 
	
	midiin->openPort(0);
	midiin->ignoreTypes(true, true, false);
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
		ArpeggioAudioStreamCallback,
		&midi
	), true);
	error_lambda(Pa_StartStream(stream), true);
	while(true) {};
	error_lambda(Pa_StopStream(stream), false);
	error_lambda(Pa_CloseStream(stream), false);
	error_lambda(Pa_Terminate(), false);
	return 0;
}

//Arpeggio Loop. Only works on monophonic midi callbacks for now. Staying here bc pretty.
static int ArpeggioAudioStreamCallback(
  const void *inputBuffer,
  void *outputBuffer,
  unsigned long framesPerBuffer,
  const PaStreamCallbackTimeInfo*,
  PaStreamCallbackFlags,
  void *userData
) {
  float *out = (float*)outputBuffer;
  MidiOutput *data = (MidiOutput*)userData;
  (void) inputBuffer;

  static float phase = 0.0f;
  static float env = 0.0f;
  static float noteTimer = 0.0f;
  static size_t arpIndex = 0;

  float attack = 0.0001f;   // instant pluck
  float release = 0.002f;   // short decay
  float arpSpeed = 0.12f;   // seconds per note
  float sampleStep = 1.0f / SAMPLE_RATE;

  // Keep track of active notes
  static std::vector<int> heldNotes;

  // Update held notes
  if (data->note != 0 && std::find(heldNotes.begin(), heldNotes.end(), data->note) == heldNotes.end())
      heldNotes.push_back(data->note);
  else if (data->note == 0)
      heldNotes.clear();

  for (unsigned int i = 0; i < framesPerBuffer; i++) {

      // Advance arpeggio timer
      noteTimer += sampleStep;
      if (noteTimer >= arpSpeed && !heldNotes.empty()) {
          noteTimer -= arpSpeed;
          arpIndex = (arpIndex + 1) % heldNotes.size();
      }

      int currentNote = heldNotes.empty() ? 0 : heldNotes[arpIndex];

      // Envelope
      float targetEnv = (currentNote != 0) ? 1.0f : 0.0f;
      if (env < targetEnv) env += attack;
      if (env > targetEnv) env -= release;
      if (env < 0.0f) env = 0.0f;
      if (env > 1.0f) env = 1.0f;

      // Frequency
      float freq = MidiToFreq(currentNote == 0 ? 60 : currentNote);
      float inc = (2.0f * M_PI * freq) / SAMPLE_RATE;
      phase += inc;
      if (phase > 2*M_PI) phase -= 2*M_PI;

      // Bell-like sine
      float sig = sinf(phase);

      // Apply envelope and velocity
      float vel = data->volume / 127.0f;
      float outSig = sig * env * vel * 0.5f;

      *out++ = outSig;
      *out++ = outSig;
  }

  return paContinue;
}

//TODO: POLYPHONY:: rtmidi handles this already- so it is solely based on our audio engine
static int SimpleAudioStreamCallback
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

	for(unsigned int i =0; i <framesPerBuffer; i++) {
		float note_phase = NotePhase(data->note);
		float note_phase_iter = note_phase/SAMPLE_RATE;
		float note_volume = data->volume;
		static float ctr = 0.0;

		if (data->note != 0) {
			ctr += note_phase_iter;
			if (ctr > (2.0f*M_PI)) {
				ctr = fmod(ctr, (2.0f*M_PI));
			}
		}
		*out++ = sin(ctr);
		*out++ = sin(ctr);
	}

  return paContinue;
}

float NotePhase(int note) {
	float note_frequency = MidiToFreq(note);
	float note_phase = FreqToPhase(note_frequency);
	return note_phase;
	
}

//TODO: noteON/OFF too strict.
void MidiStreamCallback(double deltatime, std::vector<unsigned char> *message, void *userData) {
	MidiOutput *data = (MidiOutput*)userData;

	unsigned int nbytes = message->size();
	if (nbytes <= 0) {
		data->note = 0;
		data->volume = 0.0f;
		return;
	}
	int statusbyte = message->at(0);
	int notebyte = message->at(1);
	int volumebyte = message->at(2);
	

	//DEBUGGING PURPOSES
	bool is_note_on = (statusbyte == 144) ? true : false;
  if (!is_note_on) {
		if (!isNoteBeingPlayed) {
			data->note = 0;
			data->volume = 0.0f;
			std::cout<<"[NOTE OFF]"<< std::endl;
		}
	} else {
		isNoteBeingPlayed = true;
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
