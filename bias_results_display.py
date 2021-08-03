import numpy as np
from numpy import array
from numpy import uint16, uint32
from numpy import fft
import ROOT
import os

def plot_wf(graph, wf):
    for i, sample in enumerate(np.dstack([wf]*2).ravel()):
        graph.SetPoint(i, ((i+1)//2)*2, sample)

def bias_line(bias):
    slope = 0.26368325
    return bias*slope

if __name__ == "__main__":
    data = np.load("bias_scan_results.npz")

    diff_graphs = [ROOT.TGraphErrors() for _ in range(4)]
    mean_graphs = [ROOT.TGraphErrors() for _ in range(4)]
    counts = [0 for _ in diff_graphs]


    c3 = ROOT.TCanvas("", "", 1440, 900)
    
    NUM_CHANNELS = 8

    c3.Divide(3,8)
    zoomed_graphs = [ROOT.TGraph() for _ in range(NUM_CHANNELS)]
    total_graphs = [ROOT.TGraph() for _ in range(NUM_CHANNELS)]
    ffts_graphs = [ROOT.TGraph() for _ in range(NUM_CHANNELS)]

    [zoomed_graphs[channel].SetTitle("Channel %i" % channel) for channel in range(NUM_CHANNELS)]
    [total_graphs[channel].SetTitle("Channel %i" % channel) for channel in range(NUM_CHANNELS)]
    [ffts_graphs[channel].SetTitle("Channel %i" % channel) for channel in range(NUM_CHANNELS)]

    c3.cd(3).SetLogy()
    c3.cd(6).SetLogy()
    c3.cd(9).SetLogy()
    c3.cd(12).SetLogy()
    c3.cd(3).SetLogx()
    c3.cd(6).SetLogx()
    c3.cd(9).SetLogx()
    c3.cd(12).SetLogx()
    try:
        os.remove("wf_bias_scan.gif")
    except Exception:
        pass

    fft_freqs = fft.fftfreq(4002, 2e-9)
    #fft_freqs[fft_freqs == 0] = 100e3
    freq_mask = fft_freqs >= 0
    pos_freqs = fft_freqs[freq_mask]/1e6
    min_freq = np.min(pos_freqs[pos_freqs > 0])
    pos_freqs[pos_freqs ==0] = min_freq/10.
    max_freq = np.max(pos_freqs[pos_freqs > 0])
    biases = data['biases']
    waveforms = data['results']
    for bias, res in zip(biases, waveforms):
        for channel, wf in enumerate(res[0]):
            plot_wf(zoomed_graphs[channel], wf)
            plot_wf(total_graphs[channel], wf)

            c3.cd((channel)*3 + 1)
            total_graphs[channel].GetYaxis().SetRangeUser(0, 8192)
            total_graphs[channel].Draw()
            c3.cd((channel)*3 + 2)
            zoomed_graphs[channel].Draw()

            c3.cd((channel)*3 + 3)
            wf_fft = np.abs(fft.fft(wf))[freq_mask]
            for i,(x,y)  in enumerate(zip(pos_freqs, wf_fft)):
                ffts_graphs[channel].SetPoint(i, x, y)
            ffts_graphs[channel].Draw()
            ffts_graphs[channel].GetXaxis().SetRangeUser(min_freq/10., max_freq)
        c3.Update()
        c3.Print("wf_bias_scan.gif+")
