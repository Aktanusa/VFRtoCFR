#include "stdafx.h"
#include <stdio.h>
#include <deque>
#include <memory>

#include "windows.h"
#include "avisynth.h"

#define EPSILON 0.0005

struct timestamps {
	unsigned int num;
	double start;
	double end;
};

class VFRtoCFR : public GenericVideoFilter {
public:
	VFRtoCFR(PClip _child, const char* _times, unsigned int _numfps, unsigned int _denfps, bool _dropped, IScriptEnvironment* env) : GenericVideoFilter(_child) {
		times = _times;
		numfps = _numfps;
		denfps = _denfps;
		dropped = _dropped;

		CreateMap(env);
	}
	~VFRtoCFR() {
		if (file) {
			fclose(file);
			file = NULL;
		}
	}
	PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);

private:
	void CreateMap(IScriptEnvironment* env);

	const char* times;
	unsigned int numfps;
	unsigned int denfps;
	bool dropped;

	FILE* file;

	std::unique_ptr<unsigned int[]> framemap;
};

void VFRtoCFR::CreateMap(IScriptEnvironment* env) {
	std::deque<timestamps> inTimes;

	unsigned int count = 0;
	if (fopen_s(&file, times, "r")) {
		env->ThrowError("VFRtoCFR:  Failed to open times file for reading"); 
	}
	while (!feof(file)) {
		float i = 0;
		char* line = new char[100];
		if(fgets(line, 100, file) != NULL && line[0] != '#') {
			sscanf_s(line, "%f", &i);
			timestamps time;
			time.start = (double)i / (double)1000.0;
			time.num = count;
			count++;
			inTimes.push_back(time);
		}
	}
	fclose(file);

	for (unsigned int i = 0; i < inTimes.size(); i++) {
		if (i != inTimes.size() - 1) {
			inTimes[i].end = inTimes[i + 1].start;
		}
	}
	inTimes.back().end = inTimes.back().start + (inTimes.back().start / (double)(inTimes.size() - 1));


	unsigned int frames = (int)((inTimes.back().start * (double)numfps / (double)denfps));
	double check = (double)frames / (double)numfps * (double)denfps;
	while (check < inTimes.back().end && abs(check - inTimes.back().end) > (double)EPSILON) {
		frames++;
		check = (double)frames / (double)numfps * (double)denfps;
	}

	framemap.reset(new unsigned int[frames]);
	bool drop = false;
	std::deque<timestamps> choices;
	for (unsigned int i = 0; i < frames; i++) {
		double starttime = (double)i * (double)denfps / (double)numfps;
		double endtime = (double)(i + 1) * (double)denfps / (double)numfps;

		// Remove frames that are earlier than the last frame chosen
		while (!choices.empty() && framemap[i - 1] > choices.front().num) {
			choices.pop_front();
		}
		// Remove frames than can't be a choice for this frame
		while (!choices.empty() && (starttime > choices.front().end || abs(starttime - choices.front().end) < (double)EPSILON)) {
			choices.pop_front();
		}

		// Add possible choices
		while (!inTimes.empty() && endtime > inTimes.front().start && abs(endtime - inTimes.front().start) > (double)EPSILON) {
			if (starttime < inTimes.front().end && abs(starttime - inTimes.front().end) > (double)EPSILON) {
				choices.push_back(inTimes.front());
			}
			else {
				env->ThrowError("VFRtoCFR:  Possibly duplicate timecodes!");
			}
			inTimes.pop_front();
		}

		// Should only occur with timestamps not starting at 0
		if (choices.size() == 0) {
			if (inTimes.front().num == 0) {
				framemap[i] = 0;
			}
			else {
				env->ThrowError("VFRtoCFR:  There is a bug?!");
			}
		}
		else {
			std::deque<timestamps> choicesTemp(choices);

			// choose the largest inside CFR frame
			if (choicesTemp.size() > 1) {
				double largest = 0;
				for (unsigned int j = 0; j < choicesTemp.size(); j++) {
					double start;
					if (starttime > choicesTemp[j].start) {
						start = starttime;
					}
					else {
						start = choicesTemp[j].start;
					}
					double end;
					if (endtime < choicesTemp[j].end) {
						end = endtime;
					}
					else {
						end = choicesTemp[j].end;
					}
					if (largest < (end - start)) {
						largest = end - start;
					}
				}
				for (unsigned int j = 0; j < choicesTemp.size(); j++) {
					double start;
					if (starttime > choicesTemp[j].start) {
						start = starttime;
					}
					else {
						start = choicesTemp[j].start;
					}
					double end;
					if (endtime < choicesTemp[j].end) {
						end = endtime;
					}
					else {
						end = choicesTemp[j].end;
					}
					if (abs(largest - (end - start)) > (double)EPSILON) {
						choicesTemp.erase(choicesTemp.begin() + j);
						j--;
					}
				}
			}

			// choose the closest to center of CFR frame
			if (choicesTemp.size() > 1) {
				double mid = ((endtime - starttime) / 2) + starttime;
				double smallest = endtime - starttime;
				bool found = false;
				timestamps hasMid;
				for (unsigned int j = 0; j < choicesTemp.size(); j++) {
					if (abs(mid - choicesTemp[j].start) < smallest) {
						smallest = abs(mid - choicesTemp[j].start);
					}
					if (abs(mid - choicesTemp[j].end) < smallest) {
						smallest = abs(mid - choicesTemp[j].end);
					}
					if (mid > choicesTemp[j].start && abs(mid - choicesTemp[j].start) > (double)EPSILON && mid < choicesTemp[j].end && abs(mid - choicesTemp[j].end) > (double)EPSILON) {
						hasMid = choicesTemp[j];
						found = true;
						break;
					}
				}
				if (found) {
					choicesTemp.clear();
					choicesTemp.push_back(hasMid);
				}
				else {
					for (unsigned int j = 0; j < choicesTemp.size(); j++) {
						if (abs(smallest - abs(mid - choicesTemp[j].start)) > (double)EPSILON && abs(smallest - abs(mid - choicesTemp[j].end)) > (double)EPSILON) {
							choicesTemp.erase(choicesTemp.begin() + j);
							j--;
						}
					}
				}
			}
			
			// check to see that there is a max only two left
			if (choicesTemp.size() > 2) {
				env->ThrowError("VFRtoCFR:  There is a bug?!");
			}

			// choose smallest VFR frame since less likely to be added
			if (choicesTemp.size() > 1) {
				if (abs(choicesTemp.front().end - choicesTemp.front().start - choicesTemp.back().end + choicesTemp.back().start) > (double)EPSILON) {
					if (choicesTemp.front().end - choicesTemp.front().start < choicesTemp.back().end - choicesTemp.back().start) {
						choicesTemp.pop_back();
					}
					else {
						choicesTemp.pop_front();
					}
				}
			}

			// choose the unused one
			if (choicesTemp.size() > 1 && i != 0) {
				if (framemap[i - 1] == choicesTemp.front().num) {
					choicesTemp.pop_front();
				}
				else if (framemap[i - 1] == choicesTemp.back().num) {
					choicesTemp.pop_back();
				}
			}

			// else choose first of the two if there are still two left
			framemap[i] = choicesTemp.front().num;

			// check for dropped frames
			if (i != 0 && choicesTemp.front().num - framemap[i - 1] > 1) {
				drop = true;
			}
		}
	}
	if (dropped && drop) {
		env->ThrowError("VFRtoCFR:  There were frames dropped!");
	}

	vi.SetFPS(numfps, denfps);
	vi.num_frames = frames;

}

PVideoFrame __stdcall VFRtoCFR::GetFrame(int n, IScriptEnvironment* env) {
	return child->GetFrame(framemap[n], env);
}


AVSValue __cdecl Create_VFRtoCFR(AVSValue args, void* user_data, IScriptEnvironment* env) {
	return new VFRtoCFR(args[0].AsClip(), args[1].AsString("times.txt"), args[2].AsInt(30000), args[3].AsInt(1001), args[4].AsBool(false), env);
}


extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit2(IScriptEnvironment* env) {
	env->AddFunction("VFRtoCFR", "c[times]s[numfps]i[denfps]i[dropped]b", Create_VFRtoCFR, 0);
	return 0;
}
