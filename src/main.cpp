/*                   ___            __      __   
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

int main(void)
{
  isNoteBeingPlayed = false;
  PrintAudioSlut();

  auto error_lambda = [](PaError err, bool critical) { 
    if (err != paNoError) {
      printf("error:%s\n", Pa_GetErrorText(err)); 
      if (critical) {
        std::exit(1);
      }
    }
  };

  /* MIDI */
  PaStream *stream;
  MidiOutput midi = MidiOutput{0, 0.0f};

  std::vector<RtMidi::Api> apis;
  RtMidi::getCompiledApi(apis);
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
    NULL,
    &pastream_out,
    SAMPLE_RATE,
    256,
    paNoFlag,
    SimpleAudioStreamCallback,
    &midi
  ), true);

  error_lambda(Pa_StartStream(stream), true);
  while (true) {}

  error_lambda(Pa_StopStream(stream), false);
  error_lambda(Pa_CloseStream(stream), false);
  error_lambda(Pa_Terminate(), false);
  return 0;
}

// Arpeggio Loop. Only works on monophonic midi callbacks for now. Staying here bc pretty.
static int ArpeggioAudioStreamCallback(
  const void *inputBuffer,
  void *outputBuffer,
  unsigned long framesPerBuffer,
  const PaStreamCallbackTimeInfo*,
  PaStreamCallbackFlags,
  void *userData
)
{
  float *out = (float*)outputBuffer;
  MidiOutput *data = (MidiOutput*)userData;
  (void) inputBuffer;

  static float phase = 0.0f;
  static float env = 0.0f;
  static float noteTimer = 0.0f;
  static size_t arpIndex = 0;

  float attack = 0.0001f;
  float release = 0.002f;
  float arpSpeed = 0.12f;
  float sampleStep = 1.0f / SAMPLE_RATE;

  static std::vector<int> heldNotes;

  if (data->note != 0 && std::find(heldNotes.begin(), heldNotes.end(), data->note) == heldNotes.end())
    heldNotes.push_back(data->note);
  else if (data->note == 0)
    heldNotes.clear();

  for (unsigned int i = 0; i < framesPerBuffer; i++) {
    noteTimer += sampleStep;
    if (noteTimer >= arpSpeed && !heldNotes.empty()) {
      noteTimer -= arpSpeed;
      arpIndex = (arpIndex + 1) % heldNotes.size();
    }

    int currentNote = heldNotes.empty() ? 0 : heldNotes[arpIndex];

    float targetEnv = (currentNote != 0) ? 1.0f : 0.0f;
    if (env < targetEnv) env += attack;
    if (env > targetEnv) env -= release;
    if (env < 0.0f) env = 0.0f;
    if (env > 1.0f) env = 1.0f;

    float freq = MidiToFreq(currentNote == 0 ? 60 : currentNote);
    float inc = (2.0f * M_PI * freq) / SAMPLE_RATE;
    phase += inc;
    if (phase > 2 * M_PI) phase -= 2 * M_PI;

    float sig = sinf(phase);
    float vel = data->volume / 127.0f;
    float outSig = sig * env * vel * 0.5f;

    *out++ = outSig;
    *out++ = outSig;
  }

  return paContinue;
}

// Simple mono synth
static int SimpleAudioStreamCallback(
  const void *inputBuffer,
  void *outputBuffer,
  unsigned long framesPerBuffer,
  const PaStreamCallbackTimeInfo*,
  PaStreamCallbackFlags,
  void *userData
)
{
  float *out = (float*)outputBuffer;
  MidiOutput *data = (MidiOutput*)userData;
  (void) inputBuffer;

  static float ctr = 0.0f;

  for (unsigned int i = 0; i < framesPerBuffer; i++) {
    if (data->note != 0) {
      float freq = MidiToFreq(data->note);
      float inc = (2.0f * M_PI * freq) / SAMPLE_RATE;
      ctr += inc;
      if (ctr > 2.0f * M_PI) ctr -= 2.0f * M_PI;
    }

    float sig = sinf(ctr);
    *out++ = sig;
    *out++ = sig;
  }

  return paContinue;
}

float NotePhase(int note)
{
  float note_frequency = MidiToFreq(note);
  return FreqToPhase(note_frequency);
}

void MidiStreamCallback(double deltatime, std::vector<unsigned char> *message, void *userData)
{
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

  bool is_note_on = (statusbyte == 144);
  if (!is_note_on) {
    data->note = 0;
    data->volume = 0.0f;
    isNoteBeingPlayed = false;
    std::cout << "[NOTE OFF]" << std::endl;
  } else {
    isNoteBeingPlayed = true;
    data->note = notebyte;
    data->volume = (float)volumebyte;
    std::cout << "[NOTE ON ] Note: " << data->note << " Volume: " << data->volume << std::endl;
  }
}

float FreqToPhase(float freq)
{
  return (2 * M_PI) * freq;
}

float MidiToFreq(int midiNote)
{
  return 440.0f * powf(2.0f, ((float)midiNote - 69.0f) / 12.0f);
}

float Normalize(float x)
{
  return 1.0f / sqrtf(x);
}

void PrintAudioSlut()
{
  std::cout << R"(
                   ___            __      __   
      ____ ___  ______/ (_)___  _____/ /_  __/ /_  
     / __ `/ / / / __  / / __ \/ ___/ / / / / __/   
    / /_/ / /_/ / /_/ / / /_/ (__  ) / /_/ / /_     
    \__,_/\__,_/\__,_/_/\____/____/_/\__,_/\__/  )" 
            << std::endl << std::endl;
}
