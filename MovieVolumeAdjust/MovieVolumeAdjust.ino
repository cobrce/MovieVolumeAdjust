#include <IRremote.h>

void ReadAnalogs(bool forceRead = false);

#define PIN_UNDERVOLTAGE_LED 8
#define PIN_IR_LED 9
#define PIN_MINUS_LED 10 // for simulation, to see when sending volume-
#define PIN_PLUS_LED 11  // for simulation, to see when sending volume+

#define PIN_AUDIO A1
#define PIN_DEADBAND A2
#define PIN_REACT A3
#define PIN_UNDERVOLTAGE A7

#define IR_DATA_VOL_PLUS 0x3434E817
#define IR_DATA_VOL_MINUS 0x34346897

int deadband;
byte volcounter = 0; // count number of times the volume has been decreased

class AsyncDelay
{
private:
    unsigned long startedAt;
    bool running;

public:
    AsyncDelay()
    {
        Reset();
    }

    void Reset()
    {
        startedAt = 0;
        running = false;
    }

    bool Reached(unsigned long ms)
    {
        if (!running)
        {
            startedAt = millis();
            running = true;
        }
        auto result = (millis() >= startedAt + ms);
        if (result)
            Reset();
        return result;
    }
};

class Counter
{
private:
    unsigned long currentValue;

public:
    Counter()
    {
        Reset();
    }
    void Reset()
    {
        currentValue = 0;
    }

    bool Reached(unsigned long value)
    {
        auto result = (++currentValue >= value);
        if (result)
            Reset();
        return result;
    }
};

void setup()
{
    Serial.begin(250000);

    IrSender.begin(PIN_IR_LED, DISABLE_LED_FEEDBACK); //IR LED
    pinMode(PIN_UNDERVOLTAGE_LED, OUTPUT);            //Status
    digitalWrite(PIN_UNDERVOLTAGE_LED, LOW);

    pinMode(PIN_AUDIO, INPUT);        //Audio
    pinMode(PIN_DEADBAND, INPUT);     //Deadband
    pinMode(PIN_REACT, INPUT);        //React
    pinMode(PIN_UNDERVOLTAGE, INPUT); //undervoltage

    pinMode(PIN_MINUS_LED, OUTPUT);
    pinMode(PIN_PLUS_LED, OUTPUT);
    digitalWrite(PIN_MINUS_LED, LOW);
    digitalWrite(PIN_PLUS_LED, LOW);

    ReadAnalogs(true);
}

void loop()
{
    MainFMS();
}

/*
    since the function that were called by interrupts are now called periodically from the main code flow
    by making every one record respectively "last time I run" and checking "can I run yet?", we need the 
    code to be non-blocking (by avoiding waiting for a pin, delay..etc)
*/
void YieldDelay(unsigned long ms)
{
    auto now = millis();
    while (millis() < (now + ms))
    {
        ReadAnalogs();     // done every 1000ms
        PrintSerialData(); // done every 200ms
    }
}
void RestoreVolumeToOriginalValue()
{
    for (; volcounter; volcounter--)
    {
        //IrSender.sendSAMSUNG(IR_DATA_VOL_PLUS, 32); // Vol+
        IrSender.sendSamsung(0x707,0x7,1); // thanks to roger.tannous for pointing out the new function
        digitalWrite(PIN_PLUS_LED, HIGH);
        YieldDelay(200);
        digitalWrite(PIN_PLUS_LED, LOW);
    }
}

void LowerTheVolume()
{
    volcounter++;
    // IrSender.sendSAMSUNG(IR_DATA_VOL_MINUS, 32); //Vol-
    IrSender.sendSamsung(0x707,0xb,1); // thanks to roger.tannous for pointing out the new function
    digitalWrite(PIN_MINUS_LED, HIGH);
    YieldDelay(200);
    digitalWrite(PIN_MINUS_LED, LOW);
}

byte ReactDelay = 0;
#define ANALOG_READ_DELAY_MS 1000
AsyncDelay ReadAnalogsDelay;
void ReadAnalogs(bool forceRead = false)
{
    auto now = millis();

    if (forceRead || ReadAnalogsDelay.Reached(ANALOG_READ_DELAY_MS))
    {
        deadband = map(analogRead(PIN_DEADBAND), 0, 1023, 511, 0);
        ReactDelay = map(analogRead(PIN_REACT), 1023, 0, 32, 16); // 32ms - 16ms
        digitalWrite(PIN_UNDERVOLTAGE_LED, (analogRead(PIN_UNDERVOLTAGE) < 540));
    }
}

bool IsLoud(int audio)
{
    return (audio > (511 + deadband)) || (audio < (511 - deadband));
}

bool IsQuiet(int audio)
{
    return ((audio < 540) && (audio > 480));
}

AsyncDelay FirstLoudFoundDelay,
    DetectSilenceDelay,
    LoudConfirmedDelay,
    LowerTheVolumeDelay,
    WaitForSilenceDelay,
    SilenceFoundDelay;

Counter CycleCounter,
    LoudCounter,
    QuietCounter,
    SilenceCounter;

enum State
{
    Init,
    WaitForLoud,
    LoudFound,
    LoudConfirmed,
    WaitForSilence,
    SilenceFound,
    SilenceConfirmed
};
State state = Init;

void MainFMS()
{
    ReadAnalogs(); // done every 1000ms

    PrintSerialData(); // done every 200ms

    auto audio = analogRead(PIN_AUDIO);

    switch (state)
    {
    case Init: // resets all the timers and counters and immediatly go to next state
        FirstLoudFoundDelay.Reset();
        DetectSilenceDelay.Reset();
        LoudConfirmedDelay.Reset();
        LowerTheVolumeDelay.Reset();
        WaitForSilenceDelay.Reset();
        SilenceFoundDelay.Reset();

        CycleCounter.Reset();
        LoudCounter.Reset();
        QuietCounter.Reset(); // we could use the same counter for QuietCounter and SilenceCounter, but two separate counter are more clear
        SilenceCounter.Reset();

        state = WaitForLoud;
        break;

    case WaitForLoud: // wait for a loud sound, the next state counts them.
                      //  if during the wait we get long silent period,
                      // we reset the counter of loud waves

        if (DetectSilenceDelay.Reached(ReactDelay)) // every ReactDelay increment the
            if (SilenceCounter.Reached(50))         // SilenceCounter, when reached
                LoudCounter.Reset();                // we reset the LoudCounter (counter of the number of loud waves found)

        if (IsLoud(audio)) // (audio > (511 + deadband)) || (audio < (511 - deadband));
        {
            DetectSilenceDelay.Reset(); // reset delay for silence detection
            CycleCounter.Reset();       // reset counter of silent cycles found
            state = LoudFound;          //
        }

        break;

    case LoudFound: // a loud wave found from the previous state
                    //we increment the number of times it was found
                    // if it's more than 10 then a loud part confirmed

        // using an AsyncDelay in this case is more dynamic than YieldDelay because
        // it allows us to update the delay time
        if (!FirstLoudFoundDelay.Reached(ReactDelay)) // we wait for a ReactDelay
            break;

        SilenceCounter.Reset(); // a loud wave was found so we reset silent counter

        // to make things easy : 1 wave = 6 cycles, 1 cycle = 1 ReactDelay
        if (CycleCounter.Reached(6)) // if it's the 6th cycle then..
        {
            if (LoudCounter.Reached(10)) // we increment the counter of loud waves, if it's 10 then..
            {
                state = LoudConfirmed; // it's confirmed, loud music
            }
            else
            {
                state = WaitForLoud; // wait for another one (or maybe the audio returns to silence)
            }
        }

        break;

    case LoudConfirmed:
        if (IsLoud(audio)) // it's still loud
        {
            LowerTheVolume();           // lower the volume
            CycleCounter.Reset();       // reset the counter of cycles the audio is no more loud
            LoudConfirmedDelay.Reset(); // reset the delay it takes to confirm a non-loud part
        }
        else if (LoudConfirmedDelay.Reached(ReactDelay)) // we wait for a ReactDelay
        {
            if (CycleCounter.Reached(100)) // if we left the loud part (because we sent Vol-) for more than 100 cycles then...
            {
                state = WaitForSilence; // go to the state of waiting for a quiet part (dialog)
            }
        }
        break;

    case WaitForSilence:
        if (IsQuiet(audio)) // ((audio < 540) && (audio > 480));
            state = SilenceFound;
        break;

    case SilenceFound:
        if (!IsQuiet(audio)) // still not confirmed that it's a quiet part
        {
            CycleCounter.Reset(); // reset the counter of cycles the silence remaind
            QuietCounter.Reset(); // reset the counter of quiet waves
            state = WaitForSilence;
        }
        else if (SilenceFoundDelay.Reached(ReactDelay))
        {
            if (CycleCounter.Reached(6)) // one silent wave, then...
            {
                if (QuietCounter.Reached(20)) // increment counter of quiet waves, it' reaches 20..
                {
                    state = SilenceConfirmed; // no more music
                }
            }
        }
        break;

    case SilenceConfirmed: // left the loud area to quiet one, restore volume then return to init state
        RestoreVolumeToOriginalValue();
        state = Init;
        break;
    }
}

#define VarName(var) (#var)

char *StrStates[] = {VarName(Init),
                     VarName(WaitForLoud),
                     VarName(LoudFound),
                     VarName(LoudConfirmed),
                     VarName(WaitForSilence),
                     VarName(SilenceFound),
                     VarName(SilenceConfirmed)};

AsyncDelay PrintSerialDataDelay;
void PrintSerialData()
{
    if (PrintSerialDataDelay.Reached(200))
    {
        Serial.print("state:");

        Serial.print(StrStates[state]);
        Serial.print(" audio:");
        Serial.print(analogRead(PIN_AUDIO));
        Serial.print(" hight:");
        Serial.print(511 + deadband);
        Serial.print(" low:");
        Serial.print(511 - deadband);
        Serial.print(" volcounter:");
        Serial.println(volcounter);
    }
}