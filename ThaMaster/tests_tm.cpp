#include "Source/TmDsp.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
using namespace tmx;
static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf ("ECHEC: %s\n", msg); ++fails; } else std::printf ("ok   : %s\n", msg); } while (0)

static void defaults (float* v) { for (int i = 0; i < Count; ++i) v[i] = kDefs[i].def; }
static void run (Engine& e, std::vector<float>& L, std::vector<float>& R, const float* pv, int blk)
{
    const int n = (int) L.size();
    for (int i = 0; i < n; i += blk)
    {
        const int m = std::min (blk, n - i);
        float* c[2] = { L.data() + i, R.data() + i };
        e.process (c, 2, m, pv);
    }
}
static double rms (const std::vector<float>& x, int a, int b) { double s = 0; for (int i = a; i < b; ++i) s += (double) x[(size_t) i] * x[(size_t) i]; return std::sqrt (s / (b - a)); }

static bool load (const char* f, std::vector<float>& L, std::vector<float>& R, int maxSec = 40)
{
    std::ifstream in (f, std::ios::binary); if (! in) return false;
    std::vector<short> d ((size_t) 44100 * 2 * maxSec);
    in.read ((char*) d.data(), (std::streamsize) (d.size() * 2));
    const size_t got = (size_t) in.gcount() / 4;
    L.resize (got); R.resize (got);
    for (size_t i = 0; i < got; ++i) { L[i] = d[2 * i] / 32768.f; R[i] = d[2 * i + 1] / 32768.f; }
    return got > 0;
}
static double truePeakDb (const std::vector<float>& L)   // reference : sur-echantillonnage x8 sinc long
{
    double pk = 0; const int K = 32;
    for (size_t n = K; n + K < L.size(); n += 1)
        for (int p = 1; p < 8; ++p)
        {
            double s = 0;
            for (int k = -K + 1; k <= K; ++k)
            {
                const double x = (double) k - p / 8.0, w = 0.5 + 0.5 * std::cos (kPi * x / (K + 1));
                s += L[n + (size_t) k] * (std::sin (kPi * x) / (kPi * x)) * w;
            }
            pk = std::max (pk, std::abs (s));
        }
    return linToDb (pk);
}

int main()
{
    float v[Count];
    // 1) reconstruction unitaire : tout coupe sauf la chaine neutre
    {
        defaults (v); v[specOn] = 0; v[compOn] = 0; v[anaOn] = 0; v[stOn] = 0; v[eqOn] = 0; v[clipOn] = 0; v[limOn] = 0; v[limTP] = 0;
        Engine e; e.prepare (44100, 512);
        const int n = 44100 * 3;
        std::vector<float> L ((size_t) n), R ((size_t) n), L0, R0;
        for (int i = 0; i < n; ++i) { L[(size_t) i] = 0.3f * (float) std::sin (2 * kPi * 440.0 * i / 44100.0) + 0.1f * (float) std::sin (2 * kPi * 3100.0 * i / 44100.0); R[(size_t) i] = 0.25f * (float) std::sin (2 * kPi * 523.0 * i / 44100.0); }
        L0 = L; R0 = R; run (e, L, R, v, 333);
        const int lat = e.latency(); double err = 0;
        for (int i = 44100; i < n - lat; ++i) err = std::max (err, (double) std::abs (L[(size_t) (i + lat)] - L0[(size_t) i]));
        std::printf ("latence %d, erreur max %.2e\n", lat, err);
        CHECK (err < 2e-4, "reconstruction STFT quasi parfaite (spectral a 0, tout neutre)");
    }
    // 2) fichiers reels
    const char* files[] = { "/tmp/au/maq.raw" };
    for (auto f : files)
    {
        std::vector<float> L, R;
        if (! load (f, L, R, 30)) { std::printf ("fichier absent %s\n", f); continue; }
        for (auto& x : L) x *= 0.2f; for (auto& x : R) x *= 0.2f;
        const auto L0 = L, R0 = R;
        defaults (v); v[matchOn] = 0;
        Engine e; e.prepare (44100, 512);
        run (e, L, R, v, 512);
        const int lat = e.latency();
        bool finite = true; float mx = 0;
        for (size_t i = 0; i < L.size(); ++i) { if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) finite = false; mx = std::max ({ mx, std::abs (L[i]), std::abs (R[i]) }); }
        CHECK (finite, "sortie finie");
        const double tp = truePeakDb (L);
        std::printf ("entree : LUFS int %.1f, peak %.1f dB | sortie : LUFS int %.1f, sample peak %.2f dB, true peak (ref) %.2f dB | latence %d\n",
                     e.inIntegrated(), linToDb (*std::max_element (L0.begin(), L0.end())), e.outIntegrated(), linToDb (mx), tp, lat);
        CHECK (linToDb (mx) <= -0.95, "crete echantillon <= plafond (-1 dB)");
        CHECK (tp <= -0.8, "true peak <= -0.8 dB (reference sur-echantillonnee x8)");
        CHECK (e.outIntegrated() > e.inIntegrated() + 2.0, "sonie augmentee par la chaine");
        // 3) tailles de blocs differentes -> meme resultat (a la tolerance des rampes pres)
        for (int blk : { 1, 37, 4096 })
        {
            auto A = L0, B = R0; Engine e2; e2.prepare (44100, 512); run (e2, A, B, v, blk);
            double d = 0; for (size_t i = 0; i < A.size(); ++i) d = std::max (d, (double) std::abs (A[i] - L[i]));
            std::printf ("bloc %d : ecart max %.2e\n", blk, d);
            CHECK (d < 1e-3, "independant de la taille de bloc");
        }
        // 4) AMOUNT spectral : la courbe change et la deviation est mesuree
        std::printf ("deviation RMS vs cible : %.2f dB ; correction ", e.spec.deviationRms());
        for (int q = 0; q < 64; q += 8) std::printf ("%.0fHz:%+.1f ", e.spec.curveF[(size_t) q], (double) e.spec.curveOut[(size_t) q].load());
        std::printf ("\n");
        // 5) match : sonie de sortie ~ entree
        v[matchOn] = 1; { auto A = L0, B = R0; Engine e3; e3.prepare (44100, 512); run (e3, A, B, v, 512); std::printf ("match : in %.1f out %.1f (matchDb %.1f)\n", e3.inShort(), e3.outShort(), (double) e3.matchDb.load());
            CHECK (std::abs (e3.inShort() - e3.outShort()) < 2.0, "MATCH : sonie sortie ~ entree"); }
        v[matchOn] = 0;
        // 6) monitor delta/input
        v[monitor] = 1; { auto A = L0, B = R0; Engine e4; e4.prepare (44100, 512); run (e4, A, B, v, 512); double err = 0; for (size_t i = 44100; i + (size_t) lat < A.size(); ++i) err = std::max (err, (double) std::abs (A[i + (size_t) lat] - L0[i])); CHECK (err < 1e-4, "MONITOR Input = entree alignee"); }
        v[monitor] = 0;
        // 7) bypass
        v[bypass] = 1; { auto A = L0, B = R0; Engine e5; e5.prepare (44100, 512); run (e5, A, B, v, 512); double err = 0; for (size_t i = 44100; i + (size_t) lat < A.size(); ++i) err = std::max (err, (double) std::abs (A[i + (size_t) lat] - L0[i])); CHECK (err < 1e-4, "BYPASS = entree alignee"); }
        v[bypass] = 0;
        // 8) mono (L=R) reste mono
        { auto A = L0, B = L0; Engine e6; e6.prepare (44100, 512); run (e6, A, B, v, 512); double d = 0; for (size_t i = 0; i < A.size(); ++i) d = std::max (d, (double) std::abs (A[i] - B[i])); CHECK (d < 1e-4, "entree mono -> sortie mono"); }
    }
    // 9) fuzz parametres aleatoires + frequences d'echantillonnage
    {
        std::vector<float> L, R; load ("/tmp/au/maq.raw", L, R, 6);
        srand (7); bool ok = true; float mx = 0;
        for (double fs : { 44100.0, 48000.0, 96000.0 })
            for (int run_ = 0; run_ < 6; ++run_)
            {
                for (int i = 0; i < Count; ++i) { const auto& d = kDefs[i]; float x = d.lo + (d.hi - d.lo) * (float) rand() / RAND_MAX; if (d.kind != KFloat) x = std::round (x); v[i] = x; }
                v[bypass] = 0; v[monitor] = 0;
                auto A = L, B = R; Engine e; e.prepare (fs, 512);
                for (size_t i = 0; i < A.size(); i += 777) { for (int k = 0; k < Count; ++k) if (rand() % 400 == 0) { const auto& d = kDefs[k]; v[k] = d.lo + (d.hi - d.lo) * (float) rand() / RAND_MAX; if (d.kind != KFloat) v[k] = std::round (v[k]); } v[bypass] = 0; float* c[2] = { A.data() + i, B.data() + i }; e.process (c, 2, (int) std::min<size_t> (777, A.size() - i), v); }
                for (size_t i = 0; i < A.size(); ++i) { if (! std::isfinite (A[i]) || ! std::isfinite (B[i])) ok = false; mx = std::max ({ mx, std::abs (A[i]), std::abs (B[i]) }); }
            }
        std::printf ("fuzz : max |x| = %.2f\n", mx);
        CHECK (ok && mx < 4.01f, "fuzz : toujours fini et borne");
    }
    std::printf (fails ? "\n%d ECHEC(S)\n" : "\nTOUS LES TESTS OK\n", fails);
    return fails != 0;
}
