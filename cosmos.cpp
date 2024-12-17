#include "daisy_patch.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

DaisyPatch patch;

// Reverb Objects
FeedbackDelayNetwork fdnReverb;
PlateReverb plateReverbL, plateReverbR;

// Modulation and Filtering
Oscillator lfoMod;
Svf lowPass;

// Reverb Modes
enum ReverbMode { PLATE, NEBULAE };
ReverbMode activeMode = PLATE;

// Parameters and Names
constexpr int kNumParams = 4;
float decay = 0.5f, modDepth = 0.5f, dampingFreq = 0.3f, modRate = 0.3f;
const char* paramNames[] = {"Decay", "Swirl Depth", "Damping Freq", "Mod Rate"};
float* params[kNumParams] = {&decay, &modDepth, &dampingFreq, &modRate};
int currentParam = 0;

// Graphics State
float wavePositions[10] = {};
constexpr int numWaves = 10;

// Timing for UI Display
uint32_t lastParamChangeTime = 0;
constexpr uint32_t paramDisplayTimeout = 3000; // 3 seconds

// Menu State
bool showMenu = false;
int selectedMode = 0;
uint32_t encoderPressStart = 0;
constexpr uint32_t longPressDuration = 1000; // 1 second

// Envelope Follower
float envelope = 0.0f;

// === Helper Functions ===
inline int ParamToPercent(float value) { return static_cast<int>(value * 100.0f); }

void UpdateEnvelope(float wetL, float wetR)
{
    float level = fabsf(wetL) + fabsf(wetR);
    envelope = (0.95f * envelope) + (0.05f * level);
}

void UpdateWaves()
{
    for (auto& wavePos : wavePositions)
    {
        wavePos += 2.0f;
        if (wavePos > 120.0f)
            wavePos = 0.0f;
    }
}

void DrawPlateVisualization()
{
    patch.display.Fill(false);

    // Source box
    patch.display.DrawRect(2, 28, 10, 36, true);
    // Plate
    patch.display.DrawLine(120, 10, 120, 54, true);

    for (int i = 0; i < numWaves; i++)
    {
        float alpha = 0.5f + envelope;
        int y = 32 + (i % 3) * 3 - 3;
        int x = static_cast<int>(wavePositions[i]);
        patch.display.DrawLine(12 + x, y, 120 - x, y, alpha > 0.7f);
    }

    patch.display.Update();
}

void DrawNebulaeVisualization()
{
    patch.display.Fill(false);

    for (int i = 0; i < 10; i++)
    {
        float radius = envelope * 20 + i * 2;
        patch.display.DrawCircle(64, 32, radius, false);
    }

    patch.display.Update();
}

void DrawParameterScreen()
{
    patch.display.Fill(false);

    patch.display.SetCursor(0, 0);
    patch.display.WriteString(activeMode == PLATE ? "Plate Reverb" : "Nebulae Reverb", Font_6x8, true);

    for (int i = 0; i < kNumParams; i++)
    {
        patch.display.SetCursor(0, 10 + i * 10);
        char buf[32];
        sprintf(buf, "%s: %d%%", paramNames[i], ParamToPercent(*params[i]));
        patch.display.WriteString(buf, Font_6x8, true);
    }

    patch.display.Update();
}

void DrawReverbMenu()
{
    patch.display.Fill(false);

    patch.display.SetCursor(30, 0);
    patch.display.WriteString("Reverb Mode", Font_6x8, true);

    if (selectedMode == 0)
    {
        patch.display.DrawRect(10, 15, 100, 20, true);
        patch.display.SetCursor(35, 22);
        patch.display.WriteString("Plate", Font_6x8, false);
    }
    else
    {
        patch.display.DrawRect(10, 15, 100, 20, false);
        patch.display.SetCursor(35, 22);
        patch.display.WriteString("Plate", Font_6x8, true);
    }

    if (selectedMode == 1)
    {
        patch.display.DrawRect(10, 40, 100, 20, true);
        patch.display.SetCursor(30, 47);
        patch.display.WriteString("Nebulae", Font_6x8, false);
    }
    else
    {
        patch.display.DrawRect(10, 40, 100, 20, false);
        patch.display.SetCursor(30, 47);
        patch.display.WriteString("Nebulae", Font_6x8, true);
    }

    patch.display.Update();
}

// === Encoder and Audio Logic ===
void ProcessEncoder()
{
    int inc = patch.encoder.Increment();
    bool pressed = patch.encoder.RisingEdge();
    bool held = patch.encoder.Pressed();

    if (held && encoderPressStart == 0)
        encoderPressStart = System::GetNow();

    if (encoderPressStart > 0 && System::GetNow() - encoderPressStart > longPressDuration)
    {
        showMenu = true;
        encoderPressStart = 0;
    }

    if (showMenu)
    {
        if (inc != 0)
            selectedMode = (selectedMode + inc + 2) % 2;

        if (pressed)
        {
            activeMode = selectedMode == 0 ? PLATE : NEBULAE;
            showMenu = false;
        }
    }
    else
    {
        if (inc != 0)
        {
            *params[currentParam] += inc * 0.01f;
            *params[currentParam] = fclamp(*params[currentParam], 0.0f, 1.0f);
            lastParamChangeTime = System::GetNow();
        }

        if (pressed)
        {
            currentParam = (currentParam + 1) % kNumParams;
            lastParamChangeTime = System::GetNow();
        }
    }
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        float inL = in[0][i], inR = in[1][i], wetL, wetR;

        if (activeMode == PLATE)
        {
            plateReverbL.SetDecay(decay);
            wetL = plateReverbL.Process(inL);
            wetR = plateReverbR.Process(inR);
        }
        else
        {
            fdnReverb.SetFeedback(decay * 0.9f);
            wetL = fdnReverb.Process(inL);
            wetR = fdnReverb.Process(inR);
        }

        UpdateEnvelope(wetL, wetR);
        out[0][i] = inL * 0.3f + wetL * 0.7f;
        out[1][i] = inR * 0.3f + wetR * 0.7f;
    }

    if (showMenu)
        DrawReverbMenu();
    else if (System::GetNow() - lastParamChangeTime < paramDisplayTimeout)
        DrawParameterScreen();
    else if (activeMode == PLATE)
        DrawPlateVisualization();
    else
        DrawNebulaeVisualization();
}

// === Main ===
int main()
{
    patch.Init();
    plateReverbL.Init(patch.AudioSampleRate());
    plateReverbR.Init(patch.AudioSampleRate());
    fdnReverb.Init();
    fdnReverb.SetMaxDelay(1.5f);
    lfoMod.Init(patch.AudioSampleRate());
    lfoMod.SetWaveform(Oscillator::WAVE_SIN);

    patch.StartAudio(AudioCallback);

    while (true)
        ProcessEncoder();
}
