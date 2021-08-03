import ROOT
import numpy as np

def autocorr(x):
    result = np.correlate(x, x, mode='full')
    return result[int(result.size/2)-1:] 


#fname = "ocm_scan_results_biased_DC_off_interleaving_off"
#fname = "ocm_scan_results_biased_DC_on_interleaving_off"
#fname = "ocm_scan_results_biased_DC_on_interleaving_on"
#fname = "ocm_scan_results_biased_DC_off_interleaving_on"
#fname = "ocm_scan_results_DC_off_interleaving_off"
#fname = "ocm_scan_results_DC_off_interleaving_on"
#fname = "ocm_scan_results_DC_on_interleaving_off"
#fname = "ocm_scan_results_DC_on_interleaving_on"
fname = "ocm_scan_hermes1_dc_on_interleaving_on"
#fname = "ocm_scan_results"
data = np.load(fname+".npz")
ocms = data['ocms']
results = data['results']
results = results[:, 0,:, :]

mask = np.bitwise_and(results, 1<<13)
results[mask == 0 ] += 8192
results[mask != 0 ] -= 8192

diffs = np.abs(np.diff(results.astype(np.float), axis=2))

#avg_graphs = [ROOT.TGraph() for _ in range(8)]
#med_graphs = [ROOT.TGraph() for _ in range(8)]
#avgs = np.mean(diffs, axis=2)
#meds = np.median(diffs, axis=2)
#for chan, g, in enumerate(avg_graphs):
#    for i, ocm in enumerate(ocms):
#        g.SetPoint(i,ocm, avgs[i, chan])
#
#avg_graphs = [ROOT.TGraph() for _ in range(8)]
#for chan, g, in enumerate(med_graphs):
#    for i, ocm in enumerate(ocms):
#        g.SetPoint(i,ocm, meds[i, chan])
        
mins = np.min(diffs, axis=(0,2))
maxs = np.clip( np.max(diffs, axis=(0,2)), a_min=None, a_max=500)
hists = [ROOT.TH2I("chan%i" % i, "chan%i"%i, int(ocms[-1] - ocms[0]), int(ocms[0]), int(ocms[-1]), int(maxs[i] - mins[i]), int(mins[i]), int(maxs[i]))  for i in range(0,4)]
#hists = [ROOT.TH2I("chan%i" % i, "chan%i"%i, int(ocms[-1] - ocms[0]), int(ocms[0]), int(ocms[-1]), int(maxs[i] - mins[i]), int(mins[i]), int(maxs[i]))  for i in range(4,8)]
for i, h in enumerate(hists):
   h.SetStats(0)
   #chan = i+4
   chan = i
   for i, ocm in enumerate(ocms):
        for value in diffs[i, chan]:
            h.Fill(ocm, value)

c = ROOT.TCanvas()
for i, h in enumerate(hists):
    chan = i
    #chan = i+4
    h.Draw("colz")
    c.Update()
    c.Print(fname+("_chan%i.pdf" % chan))
