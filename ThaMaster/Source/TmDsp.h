#pragma once

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>
#include "TmParams.h"

// Fck ThaMASTER : algorithmes originaux, bases sur des techniques publiques (STFT, filtres Linkwitz-Riley,
// ADAA, mesure BS.1770, limiteur a anticipation). Double precision, aucune allocation dans process().

namespace tmx
{
constexpr double kPi = 3.14159265358979323846;
inline double dbToLin (double d) noexcept { return std::pow (10.0, d / 20.0); }
inline double linToDb (double l) noexcept { return 20.0 * std::log10 (l + 1e-12); }
inline double clampd (double x, double a, double b) noexcept { return x < a ? a : (x > b ? b : x); }

//==============================================================================
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() noexcept { z1 = z2 = 0; }
    void raw (double B0, double B1, double B2, double A0, double A1, double A2) noexcept
    {
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    void lowShelf (double fs, double f, double q, double dB) noexcept
    {
        const double A = std::pow (10.0, dB / 40.0), w = 2.0 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q), t = 2.0 * std::sqrt (A) * al;
        raw (A * ((A + 1) - (A - 1) * c + t), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - t),
             (A + 1) + (A - 1) * c + t, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - t);
    }
    void highShelf (double fs, double f, double q, double dB) noexcept
    {
        const double A = std::pow (10.0, dB / 40.0), w = 2.0 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q), t = 2.0 * std::sqrt (A) * al;
        raw (A * ((A + 1) + (A - 1) * c + t), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - t),
             (A + 1) - (A - 1) * c + t, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - t);
    }
    void highPass (double fs, double f, double q) noexcept
    {
        const double w = 2.0 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        raw ((1 + c) * 0.5, -(1 + c), (1 + c) * 0.5, 1 + al, -2 * c, 1 - al);
    }
    void lowPass (double fs, double f, double q) noexcept
    {
        const double w = 2.0 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        raw ((1 - c) * 0.5, 1 - c, (1 - c) * 0.5, 1 + al, -2 * c, 1 - al);
    }
};

// Filtre Linkwitz-Riley d'ordre 4 (deux Butterworth en cascade)
struct LR4
{
    Biquad a, b;
    void lowPass (double fs, double f) { a.lowPass (fs, f, 0.70710678); b.lowPass (fs, f, 0.70710678); }
    void highPass (double fs, double f) { a.highPass (fs, f, 0.70710678); b.highPass (fs, f, 0.70710678); }
    inline double process (double x) noexcept { return b.process (a.process (x)); }
    void reset() noexcept { a.reset(); b.reset(); }
};

// tanh avec anti-aliasing par antiderivee (ADAA 1er ordre) applique au seul residu non lineaire (tanh(x)-x) :
// la partie lineaire passe intacte (pas de filtrage passe-bas parasite).
struct TanhAdaa
{
    double x1 = 0;
    static inline double F (double x) noexcept { const double a = std::abs (x); return a + std::log1p (std::exp (-2.0 * a)) - 0.6931471805599453; }
    inline double process (double x) noexcept
    {
        const double d = x - x1;
        const double m = 0.5 * (x + x1);
        const double res = std::abs (d) < 1e-4 ? std::tanh (m) - m : (F (x) - F (x1)) / d - m;
        x1 = x;
        return x + res;
    }
    void reset() noexcept { x1 = 0; }
};

struct Delay
{
    std::vector<float> buf;
    int mask = 0, w = 0;
    void init (int maxSamples)
    {
        int n = 8;
        while (n < maxSamples + 8) n <<= 1;
        buf.assign ((size_t) n, 0.f);
        mask = n - 1; w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.f); }
    inline void write (double x) noexcept { buf[(size_t) w] = (float) x; w = (w + 1) & mask; }
    // echantillon d pas en arriere du dernier ecrit (d entier)
    inline double readInt (int d) const noexcept { return (double) buf[(size_t) ((w - 1 - d) & mask)]; }
};

//==============================================================================
// SPECTRAL : equilibre tonal adaptatif a phase lineaire (STFT 2048, recouvrement 75 %).
// On mesure le spectre moyen par tiers d'octave sur plusieurs secondes, on le compare a une courbe cible de master moderne
// et on applique une correction douce et lente, proportionnelle a AMOUNT. Latence fixe : kN echantillons.
class Spectral
{
public:
    static constexpr int kN = 2048, kHop = 512, kNB = 28, kCurve = 64;
    static constexpr int kLatency = kN;

    Spectral() : fft (11) {}

    void prepare (double fs)
    {
        sr = fs; binW = fs / (double) kN;
        for (int i = 0; i < kN; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) kN));
        static const double fc[kNB] = { 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500,
                                        3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000 };
        // cible : niveau par tiers d'octave relatif a 1 kHz (dB), profil moyen d'un master moderne
        static const double tgt[kNB] = { -1, 0, 1, 2, 3, 3.5, 3.5, 3.5, 3, 2.5, 2, 1.5, 1, 0.5, 0, 0, -0.5, -1.5, -2.5, -3.5,
                                         -4.5, -5.5, -6.5, -7.5, -9, -10.5, -12.5, -16 };
        for (int b = 0; b < kNB; ++b)
        {
            fcB[(size_t) b] = fc[b]; tgtB[(size_t) b] = tgt[b];
            const double lo = fc[b] / std::pow (2.0, 1.0 / 6.0), hi = std::min (fc[b] * std::pow (2.0, 1.0 / 6.0), fs * 0.48);
            int k0 = (int) std::ceil (lo / binW), k1 = (int) std::floor (hi / binW);
            if (k1 < k0) { k0 = k1 = (int) std::lround (fc[b] / binW); }
            k0 = std::max (1, k0); k1 = std::max (k0, std::min (k1, kN / 2 - 1));
            kLo[(size_t) b] = k0; kHi[(size_t) b] = k1;
            widthScale[(size_t) b] = std::max (1.0, (hi - lo) / binW / (double) (k1 - k0 + 1));
        }
        for (int c = 0; c < kCurve; ++c) curveF[(size_t) c] = 20.0 * std::pow (1000.0, (double) c / (double) (kCurve - 1));
        reset();
    }
    void reset()
    {
        inL.fill (0); inR.fill (0); accL.fill (0); accR.fill (0);
        pos = 0; count = 0; frames = 0;
        for (auto& x : avgP) x = 0; for (auto& x : bandGain) x = 0; for (auto& x : corrDb) x = 0;
        for (auto& x : gainBin) x = 1.0f;
        for (auto& c : curveOut) c.store (0.f);
        for (auto& c : devOutBands) c.store (0.f);
        devRms.store (0.f);
    }
    void setAmount (double a) noexcept { amt = clampd (a, 0.0, 1.0); }
    void restartAverage() noexcept { frames = 0; for (auto& x : avgP) x = 0; }       // appele depuis le thread audio uniquement
    double deviationRms() const noexcept { return (double) devRms.load(); }
    std::array<std::atomic<float>, kCurve> curveOut;       // correction appliquee (dB) pour l'affichage
    std::array<std::atomic<float>, kNB> devOutBands;       // ecart mesure par rapport a la cible (dB), pour l'affichage
    std::array<double, kCurve> curveF {};

    inline void process (double& l, double& r) noexcept
    {
        inL[(size_t) pos] = (float) l; inR[(size_t) pos] = (float) r;
        pos = (pos + 1) & (kN - 1);
        ++count;
        if (count % kHop == 0 && count >= (long long) kN) frame();
        const long long p = count - 1 - kN;
        if (p >= 0) { l = (double) accL[(size_t) (p & (2 * kN - 1))]; r = (double) accR[(size_t) (p & (2 * kN - 1))]; }
        else { l = 0; r = 0; }
    }

private:
    void frame() noexcept
    {
        // fenetre des kN derniers echantillons (pos = prochain a ecrire = le plus ancien)
        double e = 0;
        for (int i = 0; i < kN; ++i)
        {
            const int j = (pos + i) & (kN - 1);
            const float w = win[(size_t) i];
            fL[(size_t) i] = inL[(size_t) j] * w; fR[(size_t) i] = inR[(size_t) j] * w;
            e += (double) inL[(size_t) j] * inL[(size_t) j] + (double) inR[(size_t) j] * inR[(size_t) j];
        }
        for (int i = kN; i < 2 * kN; ++i) { fL[(size_t) i] = 0; fR[(size_t) i] = 0; }
        fft.performRealOnlyForwardTransform (fL.data(), true);
        fft.performRealOnlyForwardTransform (fR.data(), true);
        const bool active = e / (2.0 * kN) > 1e-7;     // > -70 dBFS RMS environ

        if (active)
        {
            ++frames;
            const double alpha = std::max (1.0 / (double) frames, (double) kHop / (sr * 6.0));
            for (int b = 0; b < kNB; ++b)
            {
                double s = 0; const int k0 = kLo[(size_t) b], k1 = kHi[(size_t) b];
                for (int k = k0; k <= k1; ++k)
                {
                    const double a = fL[(size_t) (2 * k)], bq = fL[(size_t) (2 * k + 1)], c = fR[(size_t) (2 * k)], d = fR[(size_t) (2 * k + 1)];
                    s += 0.5 * (a * a + bq * bq + c * c + d * d);
                }
                const double est = s / (double) (k1 - k0 + 1) * widthScale[(size_t) b];
                avgP[(size_t) b] += (est - avgP[(size_t) b]) * alpha;
            }
            updateCorrection();
        }
        // application des gains (reels, donc a phase nulle)
        for (int k = 0; k <= kN / 2; ++k)
        {
            const float g = gainBin[(size_t) k];
            fL[(size_t) (2 * k)] *= g; fL[(size_t) (2 * k + 1)] *= g;
            fR[(size_t) (2 * k)] *= g; fR[(size_t) (2 * k + 1)] *= g;
        }
        fft.performRealOnlyInverseTransform (fL.data());
        fft.performRealOnlyInverseTransform (fR.data());
        const long long start = count - kN;
        for (int i = kN - kHop; i < kN; ++i) { const size_t q = (size_t) ((start + i) & (2 * kN - 1)); accL[q] = 0; accR[q] = 0; }
        for (int i = 0; i < kN; ++i)
        {
            const size_t q = (size_t) ((start + i) & (2 * kN - 1));
            const float w = win[(size_t) i] * (1.0f / 1.5f);
            accL[q] += fL[(size_t) i] * w; accR[q] += fR[(size_t) i] * w;
        }
    }

    void updateCorrection() noexcept
    {
        double lvl[kNB], dev[kNB];
        for (int b = 0; b < kNB; ++b) lvl[b] = 10.0 * std::log10 (avgP[(size_t) b] + 1e-30);
        // ecart par rapport a la cible, apres retrait de l'ecart moyen entre 200 Hz et 5 kHz (le niveau global ne compte pas)
        double mean = 0; int n = 0;
        for (int b = 0; b < kNB; ++b) if (fcB[(size_t) b] >= 200 && fcB[(size_t) b] <= 5000) { mean += lvl[b] - tgtB[(size_t) b]; ++n; }
        mean /= std::max (1, n);
        double ss = 0; int m = 0;
        for (int b = 0; b < kNB; ++b)
        {
            dev[b] = (lvl[b] - tgtB[(size_t) b]) - mean;
            if (fcB[(size_t) b] >= 50 && fcB[(size_t) b] <= 12500) { ss += dev[b] * dev[b]; ++m; }
        }
        devRms.store ((float) std::sqrt (ss / std::max (1, m)));
        for (int b = 0; b < kNB; ++b) devOutBands[(size_t) b].store ((float) dev[b]);
        // lissage entre bandes, puis limitation
        double c[kNB];
        for (int b = 0; b < kNB; ++b)
        {
            const double a0 = dev[std::max (0, b - 1)], a1 = dev[b], a2 = dev[std::min (kNB - 1, b + 1)];
            const double d = 0.25 * a0 + 0.5 * a1 + 0.25 * a2;
            c[b] = clampd (-d * 0.85 * amt, -6.0 * amt, 6.0 * amt);
        }
        for (int b = 0; b < kNB; ++b) bandGain[(size_t) b] += (c[b] - bandGain[(size_t) b]) * 0.15;
        // gain par bin : interpolation lineaire (en dB) selon le logarithme de la frequence
        for (int k = 0; k <= kN / 2; ++k)
        {
            const double f = std::max (1.0, (double) k * binW);
            double db;
            if (f <= fcB[0]) db = bandGain[0];
            else if (f >= fcB[kNB - 1]) db = bandGain[kNB - 1];
            else
            {
                int b = 0; while (b < kNB - 2 && f > fcB[(size_t) (b + 1)]) ++b;
                const double t = std::log (f / fcB[(size_t) b]) / std::log (fcB[(size_t) (b + 1)] / fcB[(size_t) b]);
                db = bandGain[(size_t) b] * (1.0 - t) + bandGain[(size_t) (b + 1)] * t;
            }
            gainBin[(size_t) k] = (float) dbToLin (db);
        }
        for (int q = 0; q < kCurve; ++q)
        {
            const int k = std::min (kN / 2, (int) std::lround (curveF[(size_t) q] / binW));
            curveOut[(size_t) q].store ((float) linToDb ((double) gainBin[(size_t) k]));
        }
    }

    juce::dsp::FFT fft;
    double sr = 48000, binW = 23.4, amt = 0;
    std::array<float, kN> win {}, inL {}, inR {};
    std::array<float, 2 * kN> accL {}, accR {}, fL {}, fR {};
    std::array<float, kN / 2 + 1> gainBin {};
    std::array<double, kNB> fcB {}, tgtB {}, avgP {}, bandGain {}, corrDb {}, widthScale {};
    std::array<int, kNB> kLo {}, kHi {};
    std::atomic<float> devRms { 0.f };
    int pos = 0;
    long long count = 0, frames = 0;
};

//==============================================================================
// GLUE : compression multibande douce (3 bandes, Linkwitz-Riley 4). Le seuil suit le niveau moyen de chaque bande :
// seuls les passages au-dessus de la moyenne sont "colles". GLUE regle le rapport et le seuil, DETAIL la vitesse d'attaque.
class Glue
{
public:
    std::array<std::atomic<float>, 3> grOut;   // reduction de gain par bande (dB, positif)

    void prepare (double fs)
    {
        sr = fs;
        const double f1 = 130.0, f2 = 3000.0;
        for (int c = 0; c < 2; ++c)
        {
            lp1[c].lowPass (fs, f1); lp2[c].lowPass (fs, f2);
            lp1[c].reset(); lp2[c].reset();
        }
        for (auto& b : bands) { b.avgDb = -40; b.gDb = 0; b.ms = 0; b.slow = 0; }
        msC = 1.0 - std::exp (-1.0 / (0.010 * fs));
        avgC = 1.0 - std::exp (-1.0 / (3.0 * fs));
        slowC = 1.0 - std::exp (-1.0 / (2.0 * fs));
        for (auto& g : grOut) g.store (0.f);
        grMax[0] = grMax[1] = grMax[2] = 0;
    }
    void setup (double glue, double detail)
    {
        glue_ = clampd (glue, 0.0, 1.0); detail_ = clampd (detail, 0.0, 1.0);
        const double atk = 2.0 + 28.0 * detail_;
        static const double am[3] = { 1.6, 1.0, 0.5 }, rm[3] = { 250.0, 140.0, 70.0 };
        for (int b = 0; b < 3; ++b)
        {
            bands[(size_t) b].atk = 1.0 - std::exp (-1.0 / (atk * am[b] * 0.001 * sr));
            bands[(size_t) b].rel = 1.0 - std::exp (-1.0 / (rm[b] * 0.001 * sr));
        }
        ratio = 1.0 + 3.0 * glue_;
        thrOff = 2.0 - 6.0 * glue_;
    }
    inline void process (double& l, double& r) noexcept
    {
        double x[2] = { l, r }, bd[3][2];
        for (int c = 0; c < 2; ++c)
        {
            // decoupage complementaire (somme exacte = entree, aucune rotation de phase)
            const double lo = lp1[c].process (x[c]), rest = x[c] - lo, mid = lp2[c].process (rest);
            bd[0][c] = lo; bd[1][c] = mid; bd[2][c] = rest - mid;
        }
        double o[2] = { 0, 0 };
        for (int b = 0; b < 3; ++b)
        {
            auto& s = bands[(size_t) b];
            const double pk = std::max (bd[b][0] * bd[b][0], bd[b][1] * bd[b][1]);
            s.ms += (pk - s.ms) * msC;
            const double db = 10.0 * std::log10 (s.ms + 1e-12);
            if (db > -60.0) s.avgDb += (db - s.avgDb) * avgC;
            double target = 0.0;
            if (glue_ > 0.001)
            {
                const double thr = s.avgDb + thrOff, over = db - thr, knee = 6.0;
                double gr = 0;
                if (over > knee * 0.5) gr = over * (1.0 - 1.0 / ratio);
                else if (over > -knee * 0.5) { const double t = over + knee * 0.5; gr = (1.0 - 1.0 / ratio) * t * t / (2.0 * knee); }
                target = -std::min(gr, 18.0);
            }
            s.gDb += (target - s.gDb) * (target < s.gDb ? s.atk : s.rel);
            s.slow += (s.gDb - s.slow) * slowC;
            const double g = dbToLin (s.gDb - 0.7 * s.slow);                    // compensation automatique d'une partie de la reduction moyenne
            grMax[b] = std::max (grMax[b], -s.gDb);
            o[0] += bd[b][0] * g; o[1] += bd[b][1] * g;
        }
        l = o[0]; r = o[1];
    }
    void publish() { for (int b = 0; b < 3; ++b) { grOut[(size_t) b].store ((float) grMax[b]); grMax[b] = 0; } }

private:
    struct B { double avgDb = -40, gDb = 0, ms = 0, slow = 0, atk = 0.01, rel = 0.001; };
    double sr = 48000, glue_ = 0.3, detail_ = 0.5, ratio = 1.5, thrOff = 0, msC = 0.01, avgC = 1e-5, slowC = 1e-4, grMax[3] {};
    std::array<B, 3> bands;
    LR4 lp1[2], lp2[2];
};

//==============================================================================
// ANALOG : saturation a 4 caracteres (bande ADAA, sans repliement notable).
class Analog
{
public:
    void prepare (double fs)
    {
        sr = fs;
        for (int c = 0; c < 2; ++c)
        {
            th[c].reset(); lowF[c].reset(); hpF[c].reset(); dc[c] = 0; bump[c].reset(); air[c].reset();
            lowF[c].lowPass (fs, 180.0, 0.7071); hpF[c].highPass (fs, 5000.0, 0.7071);
            bump[c].lowShelf (fs, 80.0, 0.7, 0.0); air[c].highShelf (fs, 14000.0, 0.7, 0.0);
            h2[c].reset(); h2[c].highPass (fs, 5000.0, 0.7071);
        }
    }
    void setup (double color, int type)
    {
        col = clampd (color, 0.0, 1.0); typ = type;
        drive = 1.0 + 5.0 * col;
        mix = 0.15 + 0.85 * col;
        if (std::abs (col - lastCol) > 1e-3 || typ != lastTyp)
        {
            lastCol = col; lastTyp = typ;
            for (int c = 0; c < 2; ++c)
            {
                bump[c].lowShelf (sr, 80.0, 0.7, typ == 0 ? 1.5 * col : 0.0);
                air[c].highShelf (sr, 14000.0, 0.7, typ == 0 ? -1.2 * col : 0.0);
            }
        }
    }
    inline void process (double& l, double& r) noexcept
    {
        double x[2] = { l, r };
        for (int c = 0; c < 2; ++c)
        {
            const double in = x[c];
            double wet = in;
            switch (typ)
            {
                case 0: wet = air[c].process (bump[c].process (th[c].process (in * drive) / drive)); break;                                  // bande : douce, symetrique
                case 1: { const double y = th[c].process (in * drive + 0.35 * drive * std::abs (in)) / drive; dc[c] += 0.0005 * (y - dc[c]); wet = y - dc[c]; break; }   // tube : harmoniques paires
                case 2: { const double lo = lowF[c].process (in); wet = in - lo + th[c].process (lo * drive * 1.8) / (drive * 1.8); break; }  // transfo : sature surtout les graves
                default: { const double hf = hpF[c].process (in); const double e = th[c].process (hf * drive * 3.0) / (drive * 3.0); wet = in + (e - hf) * 0.9 + 0.35 * h2[c].process (hf * std::abs (hf)) * col * 4.0; break; } // exciteur
            }
            x[c] = in + (wet - in) * mix;
        }
        l = x[0]; r = x[1];
    }
private:
    double sr = 48000, col = 0, drive = 1, mix = 0.2, lastCol = -1;
    int typ = 0, lastTyp = -1;
    TanhAdaa th[2];
    Biquad lowF[2], hpF[2], bump[2], air[2], h2[2];
    double dc[2] {};
};

//==============================================================================
// STEREO : largeur mid/side et "low mono" (graves recentres).
class Stereo
{
public:
    void prepare (double fs) { sr = fs; hp.reset(); lastF = -1; for (auto& c : cor) c = 0; cC = 1.0 - std::exp (-1.0 / (0.4 * fs)); }
    void setup (double width, bool lowMono, double freq)
    {
        wid = clampd (width, 0.0, 2.0); lm = lowMono;
        if (std::abs (freq - lastF) > 0.5) { lastF = freq; hp.lowPass (sr, freq); }
    }
    inline void process (double& l, double& r) noexcept
    {
        const double m = 0.5 * (l + r);
        double s = 0.5 * (l - r);
        const double lo = hp.process (s);
        if (lm) s -= lo;
        s *= wid;
        l = m + s; r = m - s;
    }
    // correlation (mesuree en sortie)
    inline void measure (double l, double r) noexcept { cor[0] += (l * r - cor[0]) * cC; cor[1] += (l * l - cor[1]) * cC; cor[2] += (r * r - cor[2]) * cC; }
    double correlation() const noexcept { return cor[0] / (std::sqrt (cor[1] * cor[2]) + 1e-12); }
private:
    double sr = 48000, wid = 1, lastF = -1, cC = 1e-4, cor[3] {};
    bool lm = true;
    LR4 hp;
};

//==============================================================================
// CLIP : ecreteur doux avec anti-aliasing par antiderivee. Seuil t, plafond 1.0 (asymptote).
class Clipper
{
public:
    void prepare() { x1[0] = x1[1] = 0; }
    static inline double f (double x) noexcept
    {
        const double a = std::abs (x);
        if (a <= t) return x;
        const double y = t + (1.0 - t) * std::tanh ((a - t) / (1.0 - t));
        return x < 0 ? -y : y;
    }
    static inline double F (double x) noexcept
    {
        const double a = std::abs (x);
        if (a <= t) return 0.5 * x * x;
        const double u = (a - t) / (1.0 - t);
        return 0.5 * t * t + t * (a - t) + (1.0 - t) * (1.0 - t) * (u + std::log1p (std::exp (-2.0 * u)) - 0.6931471805599453);
    }
    inline double process (int c, double x) noexcept
    {
        const double p = x1[c]; x1[c] = x;
        if (std::abs (x) <= t && std::abs (p) <= t) return x;
        const double d = x - p, m = 0.5 * (x + p);
        const double res = std::abs (d) < 1e-5 ? f (m) - m : (F (x) - F (p)) / d - m;
        return x + res;
    }
private:
    static constexpr double t = 0.80;
    double x1[2] {};
};

//==============================================================================
// Detection de crete vraie (inter-echantillons) : sur-echantillonnage x4 par FIR polyphase (sinc fenetre, 12 points par phase).
class TruePeak
{
public:
    static constexpr int kTaps = 12;
    TruePeak()
    {
        for (int p = 1; p < 4; ++p)
            for (int k = 0; k < kTaps; ++k)
            {
                const double x = (double) (k - kTaps / 2 + 1) - (double) p / 4.0;                // position relative (entre echantillons k=5 et k=6)
                const double s = std::abs (x) < 1e-9 ? 1.0 : std::sin (kPi * x) / (kPi * x);
                const double u = ((double) k - (5.0 + (double) p / 4.0)) / 6.5;
                const double w = std::abs (u) < 1.0 ? 0.42 + 0.5 * std::cos (kPi * u) + 0.08 * std::cos (2.0 * kPi * u) : 0.0;
                h[p][k] = s * std::max (0.0, w);
            }
        reset();
    }
    void reset() { hist.fill (0.0); pos = 0; }
    // pousse un echantillon ; renvoie la crete (echantillon + points interpoles) entre l'echantillon (n-6) et (n-5)
    inline double push (double x) noexcept
    {
        hist[(size_t) pos] = x; pos = (pos + 1) & 15;
        double pk = std::abs (hist[(size_t) ((pos - 1 - kTaps / 2) & 15)]);        // echantillon n-6
        for (int p = 1; p < 4; ++p)
        {
            double s = 0;
            for (int k = 0; k < kTaps; ++k) s += h[p][k] * hist[(size_t) ((pos - kTaps + k) & 15)];
            pk = std::max (pk, std::abs (s));
        }
        return pk;
    }
private:
    double h[4][kTaps] {};
    std::array<double, 16> hist {};
    int pos = 0;
};

//==============================================================================
// LIMITEUR a anticipation (3 ms), crete vraie optionnelle, relachement automatique dependant de la reduction moyenne.
class Limiter
{
public:
    static constexpr int kMax = 512;
    std::atomic<float> grOut { 0.f };

    void prepare (double fs)
    {
        sr = fs;
        la = std::min (kMax / 2 - 16, std::max (8, (int) std::lround (0.003 * fs)));
        buf[0].fill (0); buf[1].fill (0); reqBuf.fill (1.0); envBuf.fill (1.0);
        pos = 0; env = 1.0; boxSum = (double) (la + 1); avgGr = 0; grMax = 0;
        tp[0].reset(); tp[1].reset();
        grOut.store (0.f);
    }
    int latency() const noexcept { return la + 6; }
    void setup (double ceilDb, bool truePeak)
    {
        tpOn = truePeak;
        ceil = dbToLin (clampd (ceilDb, -6.0, 0.0) - (truePeak ? 0.06 : 0.0));
    }
    inline void process (double& l, double& r) noexcept
    {
        const int N = kMax;
        double pk = std::max (std::abs (l), std::abs (r));
        {
            const double a = tp[0].push (l), b = tp[1].push (r);
            pk = tpOn ? std::max (a, b) : std::max (std::abs (buf[0][(size_t) ((pos - 6) & (N - 1))]), std::abs (buf[1][(size_t) ((pos - 6) & (N - 1))]));
        }
        const double req = pk > ceil ? ceil / pk : 1.0;
        buf[0][(size_t) pos] = l; buf[1][(size_t) pos] = r; reqBuf[(size_t) pos] = req;
        double mn = 1.0;
        for (int k = 0; k <= la; ++k) mn = std::min (mn, reqBuf[(size_t) ((pos - k) & (N - 1))]);
        // relachement : lent quand la reduction moyenne est forte (son dense), rapide sinon
        const double grDb = -linToDb (env);
        avgGr += (grDb - avgGr) * 0.0004;
        const double relMs = 40.0 + 260.0 * clampd (avgGr / 5.0, 0.0, 1.0);
        const double relC = 1.0 - std::exp (-1.0 / (relMs * 0.001 * sr));
        env = mn < env ? mn : env + (mn - env) * relC;
        boxSum += env - envBuf[(size_t) ((pos - la - 1) & (N - 1))];
        envBuf[(size_t) pos] = env;
        const double g = boxSum / (double) (la + 1);
        const int rd = (pos - la - 6) & (N - 1);
        double ol = buf[0][(size_t) rd] * g, orr = buf[1][(size_t) rd] * g;
        ol = clampd (ol, -ceil, ceil); orr = clampd (orr, -ceil, ceil);      // sécurité : jamais au-dessus du plafond
        grMax = std::max (grMax, -linToDb (g));
        pos = (pos + 1) & (N - 1);
        l = ol; r = orr;
    }
    void publish() { grOut.store ((float) grMax); grMax = 0; }
private:
    double sr = 48000, ceil = 0.89, env = 1.0, boxSum = 1.0, avgGr = 0, grMax = 0;
    int la = 144, pos = 0;
    bool tpOn = true;
    std::array<double, kMax> buf[2], reqBuf, envBuf;
    TruePeak tp[2];
};

//==============================================================================
// Mesure de sonie ITU-R BS.1770 (ponderation K) : momentanee, court terme, integree (double porte).
class Loudness
{
public:
    static constexpr int kMaxBlocks = 36000 * 2;

    void prepare (double fs)
    {
        sr = fs;
        // etage 1 : plateau aigu ; etage 2 : passe-haut (coefficients de la norme, calcules pour toute frequence d'echantillonnage)
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (kPi * f0 / fs), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            for (int c = 0; c < 2; ++c) { shelf[c].b0 = (Vh + Vb * K / Q + K * K) / a0; shelf[c].b1 = 2.0 * (K * K - Vh) / a0; shelf[c].b2 = (Vh - Vb * K / Q + K * K) / a0; shelf[c].a1 = 2.0 * (K * K - 1.0) / a0; shelf[c].a2 = (1.0 - K / Q + K * K) / a0; }
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (kPi * f0 / fs), a0 = 1.0 + K / Q + K * K;
            for (int c = 0; c < 2; ++c) { hpk[c].b0 = 1; hpk[c].b1 = -2; hpk[c].b2 = 1; hpk[c].a1 = 2.0 * (K * K - 1.0) / a0; hpk[c].a2 = (1.0 - K / Q + K * K) / a0; }
        }
        reset();
    }
    void reset()
    {
        for (int c = 0; c < 2; ++c) { shelf[c].reset(); hpk[c].reset(); }
        subLen = (int) std::lround (0.1 * sr); subCnt = 0; subSum = 0;
        sub.fill (0.0); subIdx = 0; nSub = 0;
        nBlocks = 0; sinceInt = 0;
        mom.store (-120.f); st.store (-120.f); integ.store (-120.f);
        sumSq = 0; sumN = 0;
    }
    inline void push (double l, double r) noexcept
    {
        const double a = hpk[0].process (shelf[0].process (l)), b = hpk[1].process (shelf[1].process (r));
        subSum += a * a + b * b;
        if (++subCnt >= subLen)
        {
            const double ms = subSum / (double) subLen;
            subSum = 0; subCnt = 0;
            sub[(size_t) subIdx] = ms; subIdx = (subIdx + 1) % 32; nSub = std::min (32, nSub + 1);
            auto avg = [&] (int n) { double s = 0; for (int i = 0; i < n; ++i) s += sub[(size_t) ((subIdx - 1 - i + 64) % 32)]; return s / n; };
            if (nSub >= 4)
            {
                const double m = avg (4);                               // 400 ms
                mom.store ((float) toLufs (m));
                // bloc de 400 ms toutes les 100 ms pour la sonie integree
                if (nBlocks < kMaxBlocks) blocks[(size_t) nBlocks++] = m;
                else { std::copy (blocks.begin() + 1, blocks.end(), blocks.begin()); blocks[(size_t) (kMaxBlocks - 1)] = m; }
                if (m > 1.1e-7) { sumSq += m; ++sumN; }                // porte absolue ~ -70 LUFS
            }
            if (nSub >= 30) st.store ((float) toLufs (avg (30)));
            if (++sinceInt >= 5) { sinceInt = 0; computeIntegrated(); }
        }
    }
    double momentary() const noexcept { return (double) mom.load(); }
    double shortTerm() const noexcept { return (double) st.load(); }
    double integrated() const noexcept { return (double) integ.load(); }
private:
    static double toLufs (double ms) noexcept { return ms > 1e-12 ? -0.691 + 10.0 * std::log10 (ms) : -120.0; }
    void computeIntegrated() noexcept
    {
        double s = 0; int n = 0;
        for (int i = 0; i < nBlocks; ++i) if (blocks[(size_t) i] > 1.1e-7) { s += blocks[(size_t) i]; ++n; }
        if (n == 0) { integ.store (-120.f); return; }
        const double gate = toLufs (s / n) - 10.0;
        double s2 = 0; int n2 = 0;
        for (int i = 0; i < nBlocks; ++i) if (toLufs (blocks[(size_t) i]) > gate && blocks[(size_t) i] > 1.1e-7) { s2 += blocks[(size_t) i]; ++n2; }
        integ.store (n2 > 0 ? (float) toLufs (s2 / n2) : -120.f);
    }
    double sr = 48000, subSum = 0, sumSq = 0;
    int subLen = 4800, subCnt = 0, subIdx = 0, nSub = 0, nBlocks = 0, sinceInt = 0, sumN = 0;
    Biquad shelf[2], hpk[2];
    std::array<double, 32> sub {};
    std::vector<double> blocks = std::vector<double> ((size_t) kMaxBlocks, 0.0);
    std::atomic<float> mom { -120.f }, st { -120.f }, integ { -120.f };
};

//==============================================================================
struct Ramp
{
    double v = 0, c = 0.002;
    void init (double fs, double ms, double start) { c = 1.0 - std::exp (-1.0 / (ms * 0.001 * fs)); v = start; }
    inline double step (double target) noexcept { v += (target - v) * c; return v; }
};

//==============================================================================
class Engine
{
public:
    // meters (lus par l'interface)
    std::atomic<float> peakInL { 0 }, peakInR { 0 }, peakOutL { 0 }, peakOutR { 0 }, tpOut { -120 }, corr { 1 }, devOut { 0 };
    std::atomic<float> matchDb { 0 }, clipAct { 0 };
    Spectral spec;
    Glue glue;
    Limiter lim;

    void prepare (double fs, int /*maxBlock*/)
    {
        sr = fs;
        spec.prepare (fs); glue.prepare (fs); ana.prepare (fs); st.prepare (fs); clip.prepare (); lim.prepare (fs);
        lin.prepare (fs); lout.prepare (fs);
        lat = Spectral::kLatency + lim.latency();
        dry[0].init (lat + 8); dry[1].init (lat + 8);
        for (int c = 0; c < 2; ++c) { bass[c].reset(); air[c].reset(); }
        lastBass = lastAir = -999;
        rIn.init (fs, 20, 1); rOut.init (fs, 20, 1); rClip.init (fs, 20, 0); rLim.init (fs, 20, 1);
        rBy.init (fs, 15, 0); rGl.init (fs, 20, 1); rAn.init (fs, 20, 1); rSt.init (fs, 20, 1); rEq.init (fs, 20, 1); rCl.init (fs, 20, 1); rLm.init (fs, 20, 1);
        rMatch.init (fs, 800, 1); rMonIn.init (fs, 20, 0); rMonDe.init (fs, 20, 0);
        tpHold = -120; blockCounter = 0;
    }
    int latency() const noexcept { return lat; }

    void process (float* const* ch, int nch, int n, const float* pv)
    {
        const auto P = [&] (int i) { return (double) pv[i]; };
        const bool by = P (bypass) > 0.5;
        const int mon = (int) std::lround (P (monitor));
        const double inG = dbToLin (P (inGain)), outG = dbToLin (P (outGain));
        const double clipG = dbToLin (P (clipGain)), limG = dbToLin (P (limGain));
        spec.setAmount (P (specOn) > 0.5 ? P (specAmt) / 100.0 : 0.0);
        glue.setup (P (compGlue) / 100.0, P (compDetail) / 100.0);
        ana.setup (P (anaColor) / 100.0, (int) std::lround (P (anaType)));
        st.setup (P (stWidth) / 100.0, P (stLM) > 0.5, P (stLMFreq));
        lim.setup (P (limCeil), P (limTP) > 0.5);
        if (std::abs (P (eqBass) - lastBass) > 1e-4 || std::abs (P (eqAir) - lastAir) > 1e-4)
        {
            lastBass = P (eqBass); lastAir = P (eqAir);
            for (int c = 0; c < 2; ++c) { bass[c].lowShelf (sr, 110.0, 0.6, lastBass); air[c].highShelf (sr, 11000.0, 0.6, lastAir); }
        }
        const double ceilLin = dbToLin (P (limCeil));
        const bool matchOn_ = P (matchOn) > 0.5;
        float pIL = 0, pIR = 0, pOL = 0, pOR = 0, ca = 1.0f;

        for (int i = 0; i < n; ++i)
        {
            const double gi = rIn.step (inG);
            double l = (double) ch[0][i] * gi;
            double r = (double) (nch > 1 ? ch[1][i] : ch[0][i]) * gi;
            pIL = std::max (pIL, (float) std::abs (l)); pIR = std::max (pIR, (float) std::abs (r));
            lin.push (l, r);
            dry[0].write (l); dry[1].write (r);
            const double dl = dry[0].readInt (lat), dr = dry[1].readInt (lat);

            spec.process (l, r);
            { double a = l, b = r; glue.process (a, b); const double k = rGl.step (P (compOn) > 0.5 ? 1.0 : 0.0); l += (a - l) * k; r += (b - r) * k; }
            { double a = l, b = r; ana.process (a, b); const double k = rAn.step (P (anaOn) > 0.5 ? 1.0 : 0.0); l += (a - l) * k; r += (b - r) * k; }
            { double a = l, b = r; st.process (a, b); const double k = rSt.step (P (stOn) > 0.5 ? 1.0 : 0.0); l += (a - l) * k; r += (b - r) * k; }
            { double a = bass[0].process (air[0].process (l)), b = bass[1].process (air[1].process (r)); const double k = rEq.step (P (eqOn) > 0.5 ? 1.0 : 0.0); l += (a - l) * k; r += (b - r) * k; }
            {
                const double kc = rCl.step (P (clipOn) > 0.5 ? 1.0 : 0.0);
                const double g = 1.0 + (clipG - 1.0) * kc;
                const double a = clip.process (0, l * g), b = clip.process (1, r * g);
                ca = std::max (ca, (float) (std::max (std::abs (l * g), std::abs (r * g)) / (std::max (std::abs (a), std::abs (b)) + 1e-9)));
                l = l * (1.0 - kc) + a * kc; r = r * (1.0 - kc) + b * kc;
            }
            {
                const double kl = rLm.step (P (limOn) > 0.5 ? 1.0 : 0.0);
                const double g = 1.0 + (limG - 1.0) * kl;
                double a = l * g, b = r * g;
                lim.process (a, b);
                // limiteur coupe : on garde le meme retard, et un ecreteur de securite
                l = a; r = b;
                (void) kl;
            }
            st.measure (l, r);
            const double mg = rMatch.step (matchOn_ ? matchTarget : 1.0);
            const double go = rOut.step (outG) * mg;
            l *= go; r *= go;

            // monitor / bypass
            const double kb = rBy.step (by ? 1.0 : 0.0);
            const double mi = rMonIn.step (mon == 1 ? 1.0 : 0.0), md = rMonDe.step (mon == 2 ? 1.0 : 0.0);
            double ol = l, or_ = r;
            ol = ol * (1.0 - md) + (l - dl * go) * md;  or_ = or_ * (1.0 - md) + (r - dr * go) * md;
            ol = ol * (1.0 - mi) + dl * outG * mi;     or_ = or_ * (1.0 - mi) + dr * outG * mi;
            ol = ol * (1.0 - kb) + dl * outG * kb;     or_ = or_ * (1.0 - kb) + dr * outG * kb;
            ol = clampd (ol, -4.0, 4.0); or_ = clampd (or_, -4.0, 4.0);
            lout.push (ol, or_);
            pOL = std::max (pOL, (float) std::abs (ol)); pOR = std::max (pOR, (float) std::abs (or_));
            ch[0][i] = (float) ol;
            if (nch > 1) ch[1][i] = (float) or_;
        }
        (void) ceilLin;
        // mesures par bloc
        const float rel = (float) std::exp (-(double) n / (0.6 * sr));
        auto hold = [&] (std::atomic<float>& a, float v) { const float o = a.load() * rel; a.store (std::max (o, v)); };
        hold (peakInL, pIL); hold (peakInR, pIR); hold (peakOutL, pOL); hold (peakOutR, pOR);
        const float tpDb = (float) linToDb (std::max (pOL, pOR));
        tpHold = std::max ((double) tpDb, tpHold - 6.0 * n / sr);
        tpOut.store ((float) tpHold);
        corr.store ((float) st.correlation());
        devOut.store ((float) spec.deviationRms());
        glue.publish(); lim.publish();
        clipAct.store ((float) (20.0 * std::log10 ((double) std::max (1.0f, ca))));
        // match : ecart de sonie court terme entre entree et sortie
        {
            const double a = lin.shortTerm(), b = lout.shortTerm();
            if (a > -60 && b > -60 && matchOn_ && mon == 0)
            {
                const double d = clampd (a - b, -12.0, 12.0);
                matchAcc += (d - matchAcc) * std::min (1.0, (double) n / (sr * 1.5));
                matchTarget = dbToLin (matchAcc);
            }
            else if (!matchOn_) { matchAcc = 0; matchTarget = 1.0; }
            matchDb.store ((float) matchAcc);
        }
    }

    // sonie
    double inShort() const noexcept { return lin.shortTerm(); }
    double inMomentary() const noexcept { return lin.momentary(); }
    double inIntegrated() const noexcept { return lin.integrated(); }
    double outMomentary() const noexcept { return lout.momentary(); }
    double outShort() const noexcept { return lout.shortTerm(); }
    double outIntegrated() const noexcept { return lout.integrated(); }
    void resetLoudness() { lin.reset(); lout.reset(); }

private:
    double sr = 48000, lastBass = 0, lastAir = 0, tpHold = -120, matchAcc = 0, matchTarget = 1.0;
    int lat = Spectral::kLatency + 150, blockCounter = 0;
    Analog ana; Stereo st; Clipper clip;
    Loudness lin, lout;
    Delay dry[2];
    Biquad bass[2], air[2];
    Ramp rIn, rOut, rClip, rLim, rBy, rGl, rAn, rSt, rEq, rCl, rLm, rMatch, rMonIn, rMonDe;
};

} // namespace tmx
