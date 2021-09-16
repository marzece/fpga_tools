all: data_plotter freq_sweep_analyzer

data_plotter: data_plotter.o data_parser.o
	g++ -g -o $@ $^ `root-config --cflags --libs` -L`root-config --libdir`

freq_sweep_analyzer: freq_sweep_analyzer.o
	g++ -g -o $@ $^ `root-config --cflags --libs` -L`root-config --libdir`

freq_sweep_analyzer.o: freq_sweep_analyzer.c
	g++ -g -c -o $@ $^ `root-config --cflags`

data_plotter.o: data_plotter.c
	g++ -g -c -o $@ $^ `root-config --cflags`
data_parser.o: data_parser.c
	g++ -g -c -o $@ $^

clean:
	rm -rf *.o data_plotter
