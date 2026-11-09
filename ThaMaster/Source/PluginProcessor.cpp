#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace tmx;

juce::AudioProcessorValueTreeState::ParameterLayout ThaMasterProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    for (int i = 0; i < Count; ++i)
    {
        const auto& d = kDefs[i];
        const juce::String name = juce::String (d.id);
        const juce::ParameterID pid { d.id, 1 };
        if (d.kind == KBool)
            l.add (std::make_unique<juce::AudioParameterBool> (pid, name, d.def > 0.5f));
        else if (d.kind == KChoice)
        {
            juce::StringArray choices; choices.addTokens (d.unit, "|", "");
            l.add (std::make_unique<juce::AudioParameterChoice> (pid, name, choices, (int) d.def));
        }
        else
            l.add (std::make_unique<juce::AudioParameterFloat> (pid, name, juce::NormalisableRange<float> (d.lo, d.hi, d.step, d.skew), d.def));
    }
    return l;
}

ThaMasterProcessor::ThaMasterProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoMgr, "STATE", createLayout())
{
    for (int i = 0; i < Count; ++i) { raw[(size_t) i] = apvts.getRawParameterValue (kDefs[i].id); cur[(size_t) i] = kDefs[i].def; }
}

bool ThaMasterProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void ThaMasterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    engine.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (engine.latency());
    autoState.store (0);
}

void ThaMasterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int ch = buffer.getNumChannels(), n = buffer.getNumSamples();
    if (ch < 1 || n < 1) return;

    if (resetReq.exchange (false)) engine.resetLoudness();
    if (listenReset.exchange (false)) engine.spec.restartAverage();
    for (int i = 0; i < Count; ++i) cur[(size_t) i] = raw[(size_t) i]->load();

    float* ptr[2] = { buffer.getWritePointer (0), buffer.getWritePointer (ch > 1 ? 1 : 0) };
    engine.process (ptr, ch > 1 ? 2 : 1, n, cur.data());

    // AUTO : on accumule du temps de signal reellement present
    const int st = autoState.load();
    if (st != 0)
    {
        const double level = st == 2 ? engine.outShort() : (st == 5 ? engine.inMomentary() : engine.inShort());
        if (level > -55.0) activeTime += (double) n / sr;
        if (st == 5 && level > -55.0)
        {
            corrSum += (double) engine.corr.load() * n; corrW += n;
            peakMaxDb = std::max (peakMaxDb, (float) tmx::linToDb ((double) std::max (engine.peakInL.load(), engine.peakInR.load())));
            stTimer += (double) n / sr;
            if (stTimer >= 0.5 && stCount < 64) { stTimer = 0; const double m = engine.inMomentary(); if (m > -70.0) stBuf[(size_t) stCount++] = (float) m; }
        }
        autoProg.store ((float) std::min (1.0, activeTime / autoNeeded));
        if (activeTime >= autoNeeded)
        {
            if (st == 1) { pendA.store ((float) engine.inIntegrated()); pendingKind.store (1); }
            else if (st == 2) { pendA.store ((float) engine.outIntegrated()); pendingKind.store (2); }
            else if (st == 3) { pendA.store ((float) engine.inIntegrated()); pendingKind.store (3); }
            else if (st == 5)
            {
                for (int i = 0; i < 28; ++i) devSnap[(size_t) i] = engine.spec.devOutBands[(size_t) i].load();
                devRmsSnap = (float) engine.spec.deviationRms();
                corrSnap = corrW > 0 ? (float) (corrSum / corrW) : 1.f;
                peakSnap = peakMaxDb; integSnap = (float) engine.inIntegrated();
                float srt[64]; const int k = stCount; for (int i = 0; i < k; ++i) srt[i] = stBuf[(size_t) i];
                std::sort (srt, srt + k);
                spreadSnap = k >= 8 ? srt[(int) (k * 0.95f) - 1 < 0 ? 0 : (int) (k * 0.95f) - 1] - srt[(int) (k * 0.10f)] : 0.f;
                pendA.store (integSnap); pendingKind.store (5);
            }
            autoState.store (0);
            triggerAsyncUpdate();
        }
    }
}

void ThaMasterProcessor::setParamNorm (int idx, float v)
{
    if (auto* p = apvts.getParameter (kDefs[idx].id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}

void ThaMasterProcessor::startAuto()
{
    activeTime = 0; autoNeeded = 8.0; autoProg.store (0.f);
    resetReq.store (true);
    autoState.store (1);
}
void ThaMasterProcessor::startListen()
{
    activeTime = 0; autoNeeded = 12.0; autoProg.store (0.f);
    corrSum = corrW = 0; peakMaxDb = -120.f; stCount = 0; stTimer = 0;
    resetReq.store (true); listenReset.store (true);
    autoState.store (5);
}
void ThaMasterProcessor::startAutoInput()
{
    activeTime = 0; autoNeeded = 4.0; autoProg.store (0.f);
    resetReq.store (true);
    autoState.store (3);
}

static bool spreadSpread (float lu) { return lu > 8.f; }

void ThaMasterProcessor::handleAsyncUpdate()
{
    static const float targets[4] = { -14.f, -11.f, -9.f, -7.f };
    const float tgt = targets[juce::jlimit (0, 3, (int) std::lround (getParam (target)))];
    const int kind = pendingKind.exchange (0);
    const float meas = pendA.load();
    if (kind == 1 && meas > -60.f)
    {
        // phase 1 : on regle CLIP et LIMITER pour atteindre la cible a partir de la sonie d'entree
        const float need = tgt - meas;
        const float clipG = juce::jlimit (0.f, 3.f, need * 0.2f);
        const float limG = juce::jlimit (0.f, 15.f, need - clipG * 0.6f);
        setParamNorm (clipOn, 1.f); setParamNorm (limOn, 1.f); setParamNorm (matchOn, 0.f);
        setParamNorm (clipGain, clipG); setParamNorm (limGain, limG);
        activeTime = 0; autoNeeded = 8.0; autoProg.store (0.f);
        resetReq.store (true);
        autoState.store (2);
    }
    else if (kind == 5 && meas > -60.f)
    {
        auto mean = [&] (int a, int b) { float s = 0; for (int i = a; i <= b; ++i) s += devSnap[(size_t) i]; return s / (float) (b - a + 1); };
        const float mid = mean (8, 21);                             // 200 Hz - 4 kHz : reference
        const float lowRel = mean (2, 7) - mid, highRel = mean (24, 27) - mid;
        const float plr = peakSnap - integSnap;                     // dynamique crete / sonie
        // tonalite
        setParamNorm (specOn, 1.f); setParamNorm (specAmt, juce::jlimit (25.f, 85.f, 30.f + 6.f * devRmsSnap));
        setParamNorm (eqOn, 1.f);
        setParamNorm (eqBass, juce::jlimit (-3.f, 3.f, -lowRel * 0.35f));
        setParamNorm (eqAir,  juce::jlimit (-3.f, 3.f, -highRel * 0.30f));
        // dynamique
        float glue = plr > 17.f ? 50.f : plr > 14.f ? 40.f : plr > 11.f ? 30.f : plr > 9.f ? 20.f : 10.f;
        if (spreadSpread (spreadSnap)) glue += 5.f;
        setParamNorm (compOn, 1.f); setParamNorm (compGlue, glue); setParamNorm (compDetail, plr < 10.f ? 70.f : 45.f);
        // couleur
        setParamNorm (anaOn, 1.f);
        if (highRel < -3.f)      { setParamNorm (anaType, 3.f); setParamNorm (anaColor, 30.f); }
        else if (lowRel < -3.f)  { setParamNorm (anaType, 2.f); setParamNorm (anaColor, 35.f); }
        else if (plr > 14.f)     { setParamNorm (anaType, 0.f); setParamNorm (anaColor, 30.f); }
        else                     { setParamNorm (anaType, 1.f); setParamNorm (anaColor, 20.f); }
        // stereo
        setParamNorm (stOn, 1.f); setParamNorm (stLM, 1.f);
        setParamNorm (stWidth, corrSnap > 0.92f ? 130.f : corrSnap > 0.7f ? 115.f : corrSnap > 0.3f ? 105.f : 92.f);
        // sonie : meme logique que AUTO
        const float need = tgt - meas;
        const float clipG = juce::jlimit (0.f, 3.f, need * 0.2f);
        setParamNorm (clipOn, 1.f); setParamNorm (limOn, 1.f); setParamNorm (matchOn, 0.f);
        setParamNorm (clipGain, clipG); setParamNorm (limGain, juce::jlimit (0.f, 15.f, need - clipG * 0.6f));
        activeTime = 0; autoNeeded = 8.0; autoProg.store (0.f);
        resetReq.store (true);
        autoState.store (2);
    }
    else if (kind == 2 && meas > -60.f)
    {
        // phase 2 : correction fine d'apres la sonie reellement obtenue
        const float err = tgt - meas;
        setParamNorm (limGain, juce::jlimit (0.f, 15.f, getParam (limGain) + err * 0.9f));
        resetReq.store (true);
    }
    else if (kind == 3 && meas > -60.f)
    {
        setParamNorm (inGain, juce::jlimit (-18.f, 18.f, getParam (inGain) + (-18.f - meas)));
        resetReq.store (true);
    }
}

//==============================================================================
const juce::StringArray& ThaMasterProcessor::presetNames()
{
    static const juce::StringArray n { "Init (neutre)", "Streaming -14", "Hip-Hop / Trap fort", "Club -9", "Pop large", "Warm Analog", "Clean Transparent", "Loud & Punchy" };
    return n;
}

void ThaMasterProcessor::applyPreset (int index)
{
    struct V { const char* id; float v; };
    static const std::vector<std::vector<V>> presets = {
        { },
        { { "target", 0 }, { "specAmt", 40 }, { "compGlue", 30 }, { "anaColor", 15 }, { "limGain", 2.5f }, { "clipGain", 0 } },
        { { "target", 3 }, { "specAmt", 55 }, { "compGlue", 45 }, { "compDetail", 40 }, { "anaColor", 35 }, { "anaType", 2 }, { "stLMFreq", 140 }, { "eqBass", 1.0f }, { "clipGain", 2.5f }, { "limGain", 7.0f } },
        { { "target", 2 }, { "specAmt", 50 }, { "compGlue", 40 }, { "anaColor", 30 }, { "anaType", 1 }, { "eqBass", 0.8f }, { "eqAir", 0.5f }, { "clipGain", 1.5f }, { "limGain", 5.5f } },
        { { "target", 1 }, { "specAmt", 45 }, { "compGlue", 30 }, { "stWidth", 125 }, { "eqAir", 1.2f }, { "anaColor", 20 }, { "anaType", 3 }, { "limGain", 4.5f } },
        { { "target", 1 }, { "specAmt", 35 }, { "compGlue", 40 }, { "anaColor", 55 }, { "anaType", 0 }, { "eqBass", 0.8f }, { "eqAir", -0.5f }, { "limGain", 4.0f } },
        { { "target", 1 }, { "specAmt", 30 }, { "compGlue", 20 }, { "compDetail", 70 }, { "anaColor", 8 }, { "clipOn", 0 }, { "limGain", 3.0f } },
        { { "target", 3 }, { "specAmt", 60 }, { "compGlue", 55 }, { "compDetail", 25 }, { "anaColor", 40 }, { "clipGain", 3.5f }, { "limGain", 8.0f }, { "stWidth", 110 } }
    };
    index = juce::jlimit (0, (int) presets.size() - 1, index);
    for (int i = 0; i < Count; ++i)
    {
        if (kDefs[i].id == std::string ("bypass") || kDefs[i].id == std::string ("inGain") || kDefs[i].id == std::string ("outGain") || kDefs[i].id == std::string ("monitor") || kDefs[i].id == std::string ("matchOn")) continue;
        setParamNorm (i, kDefs[i].def);
    }
    for (auto& v : presets[(size_t) index])
        for (int i = 0; i < Count; ++i)
            if (kDefs[i].id == std::string (v.id)) setParamNorm (i, v.v);
    currentPreset = index;
}

void ThaMasterProcessor::resetAll()
{
    for (int i = 0; i < Count; ++i) setParamNorm (i, kDefs[i].def);
    currentPreset = 0;
}

//==============================================================================
void ThaMasterProcessor::captureSlot (int s) { for (int i = 0; i < Count; ++i) slots[(size_t) s][(size_t) i] = raw[(size_t) i]->load(); slotValid[(size_t) s] = true; }
void ThaMasterProcessor::loadSlot (int s) { for (int i = 0; i < Count; ++i) setParamNorm (i, slots[(size_t) s][(size_t) i]); }
void ThaMasterProcessor::switchAB()
{
    captureSlot (abActive);
    const int o = 1 - abActive;
    if (! slotValid[(size_t) o]) captureSlot (o);
    abActive = o; loadSlot (o);
}
void ThaMasterProcessor::copyToOther() { captureSlot (abActive); slots[(size_t) (1 - abActive)] = slots[(size_t) abActive]; slotValid[(size_t) (1 - abActive)] = true; }

void ThaMasterProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("preset", currentPreset, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}
void ThaMasterProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto t = juce::ValueTree::fromXml (*xml);
            currentPreset = (int) t.getProperty ("preset", 0);
            apvts.replaceState (t);
        }
}

juce::AudioProcessorEditor* ThaMasterProcessor::createEditor() { return new ThaMasterEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ThaMasterProcessor(); }
