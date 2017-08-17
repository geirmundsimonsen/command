#include "stdafx.h"

#define REAPERAPI_IMPLEMENT

#include "reaper_plugin.h" 
#include "reaper_plugin_functions.h"

#include <chrono>
#include <thread>
#include <fstream>

void log(std::string str) {
	std::ofstream outfile;
	outfile.open("C:/Users/User/Desktop/log.txt", std::ios::app);
	outfile << str << std::endl;
}

void print(std::string str) {
	ShowConsoleMsg(str.c_str());
}

void print(double val) {
	ShowConsoleMsg(std::to_string(val).c_str());
}

void print(int val) {
	ShowConsoleMsg(std::to_string(val).c_str());
}

void print(bool b) {
	if (b) {
		ShowConsoleMsg("true");
	} else {
		ShowConsoleMsg("false");
	}
}

#undef small
enum class FolderCompacting { normal = 0, small, tiny };
enum class AutomationMode { trimOrOff = 0, read, touch, write, latch };
enum class RecordMonitor { off = 0, normal, notWhenPlaying };
enum class RecordMode { input = 0, stereoOut, none, stereoOutLatComp, midiOutput, monoOut, monoOutLatComp, midiOverdub, midiReplace };
enum class InputType { none, audio, rearoute, MIDI };
enum class MidiChannel { omni = 0, ch1, ch2, ch3, ch4, ch5, ch6, ch7, ch8, ch9, ch10, ch11, ch12, ch13, ch14, ch15, ch16 };
enum class MidiHardware { hw0 = 0, hw1, hw2, hw3, hw4, hw5, hw6, hw7, hw8, vkb = 62, all = 63 };
enum class PanMode { classic3_x = 0, newBalance = 3, stereoPan = 5, dualPan = 6 };

class Input {
public:
	InputType type = InputType::none;
	int audioChannel = 0;
	bool isStereo = false;
	MidiChannel midiChannel = MidiChannel::omni;
	MidiHardware midiHardware = MidiHardware::all;
};

enum class SOLO { not_soloed, solo, soloed_in_place };

std::tuple<double, double> getSelection() {
	double start;
	double end;
	GetSet_LoopTimeRange2(nullptr, false, false, &start, &end, false);
	return std::make_tuple(start, end);
}

void setSelection(double start, double end) {
	//isLoop, auto seek - doesn't understand the effect
	GetSet_LoopTimeRange2(nullptr, true, false, &start, &end, false);
}

class Track {
	MediaTrack* track;
	char buf[128];

public:
	Track(MediaTrack* t) {
		track = t;
	}

	Track(int trackIndex) {
		track = GetTrack(nullptr, trackIndex);
	}

	MediaTrack* getTrack() {
		return track;
	}

	std::string getName() {
		GetSetMediaTrackInfo_String(track, "P_NAME", buf, false);
		return std::string{ buf };
	}

	void setName(const std::string& name) {
		strcpy(buf, name.c_str());
		GetSetMediaTrackInfo_String(track, "P_NAME", buf, true);
	}

	bool getMute() {
		return GetMediaTrackInfo_Value(track, "B_MUTE") != 0;
	}

	void setMute(bool mute) {
		SetMediaTrackInfo_Value(track, "B_MUTE", static_cast<double>(mute));
	}

	bool getPhase() {
		return GetMediaTrackInfo_Value(track, "B_PHASE") != 0;
	}

	void setPhase(bool invertPhase) {
		SetMediaTrackInfo_Value(track, "B_PHASE", static_cast<double>(invertPhase));
	}

	// 0 if not found, -1 for master track.
	int getTrackNumber() {
		return static_cast<int>(GetMediaTrackInfo_Value(track, "IP_TRACKNUMBER"));
	}

	SOLO getSolo() {
		double val = GetMediaTrackInfo_Value(track, "I_SOLO");
		if (val == 0)
			return SOLO::not_soloed;
		else if (val == 1)
			return SOLO::solo;
		else
			return SOLO::soloed_in_place;
	}

	void setSolo(SOLO solo) {
		SetMediaTrackInfo_Value(track, "I_SOLO", static_cast<double>(solo));
	}

	bool getFXEnabled() {
		return GetMediaTrackInfo_Value(track, "I_FXEN") != 0;
	}

	void setFXEnabled(bool enabled) {
		SetMediaTrackInfo_Value(track, "I_FXEN", static_cast<double>(enabled));
	}

	bool getRecordArmed() {
		return GetMediaTrackInfo_Value(track, "I_RECARM") != 0;
	}

	void setRecordArmed(bool recordArmed) {
		SetMediaTrackInfo_Value(track, "I_RECARM", static_cast<double>(recordArmed));
	}

	// <0: no input, 0..n: mono hardware input, 512+n: rearoute input, 1024: set for stereo input pair
	// 4096: set for MIDI input. If set, 5 low bits represent channel (0 omni, 1-16 chan), 
	// next 5 bits represent physical input(63=all, 62=VKB)
	Input getRecordInput() {
		Input input;
		int val = static_cast<int>(GetMediaTrackInfo_Value(track, "I_RECINPUT"));
		bool isMidi = (val >> 12 & 1) == 1; // midibit: 4096. op. preced. >> before &.

		if (isMidi) {
			input.type = InputType::MIDI;
			int chan = val & 0b0001'1111;
			if (chan <= 16) {
				input.midiChannel = static_cast<MidiChannel>(chan);
			} else {
				log("midi channel parse failed");
			}
			int hw = val >> 5 & 0b0011'1111;
			if (hw <= 8) {
				input.midiHardware = static_cast<MidiHardware>(hw);
			} else if (hw == 62) {
				input.midiHardware = MidiHardware::vkb;
			} else if (hw == 63) {
				input.midiHardware = MidiHardware::all;
			} else {
				log("midi hardware parse failed");
			}
			return input;
		} else {
			if (val < 0) {
				input.type = InputType::none;
				return input;
			} else if ((val & 0b0011'1111'1111) < 512) {
				input.type = InputType::audio;
				input.isStereo = (val >> 10 & 1) == 1; // stereobit: 1024.
				input.audioChannel = val & 0b0000'0001'1111'1111;
				return input;
			} else {
				input.type = InputType::rearoute; // don't care
				log("unknown input type?");
				return input;
			}
		}
	}

	// midi hardware device index refers to the midi device list in preferences. My keyboard is at idx 3,
	// the rest are disabled.
	void setRecordInput(Input input) {
		int val = -1;
		if (input.type == InputType::MIDI) {
			val = 4096 + static_cast<int>(input.midiChannel) + 32 * static_cast<int>(input.midiHardware);
		} else if (input.type == InputType::audio) {
			if (input.isStereo && input.audioChannel >= 0 && input.audioChannel <= 510) {
				val = 1024 + input.audioChannel;
			} else if (!input.isStereo && input.audioChannel >= 0 && input.audioChannel <= 511) {
				val = input.audioChannel;
			} else {
				log("wrongly formatted input");
			}
		}
		SetMediaTrackInfo_Value(track, "I_RECINPUT", val);
	}

	RecordMode getRecordMode() {
		return static_cast<RecordMode>(static_cast<int>(GetMediaTrackInfo_Value(track, "I_RECMODE")));
	}

	void setRecordMode(RecordMode mode) {
		SetMediaTrackInfo_Value(track, "I_RECMODE", static_cast<double>(mode));
	}

	RecordMonitor getRecordMonitor() {
		return static_cast<RecordMonitor>(static_cast<int>(GetMediaTrackInfo_Value(track, "I_RECMON")));
	}

	void setRecordMonitor(RecordMonitor monitor) {
		SetMediaTrackInfo_Value(track, "I_RECMON", static_cast<double>(monitor));
	}

	bool getMonitorItems() {
		return GetMediaTrackInfo_Value(track, "I_RECMONITEMS") != 0;
	}

	void setMonitorItems(bool monitorItems) {
		SetMediaTrackInfo_Value(track, "I_RECMONITEMS", static_cast<double>(monitorItems));
	}

	AutomationMode getAutomationMode() {
		return static_cast<AutomationMode>(static_cast<int>(GetMediaTrackInfo_Value(track, "I_AUTOMODE")));
	}

	void setAutomationMode(AutomationMode mode) {
		SetMediaTrackInfo_Value(track, "I_AUTOMODE", static_cast<double>(mode));
	}

	int getNrOfChannels() {
		return static_cast<int>(GetMediaTrackInfo_Value(track, "I_NCHAN"));
	}

	// has to be even; range 2-64
	void setNrOfChannels(int nrOfChannels) {
		if (nrOfChannels >= 2 && nrOfChannels <= 64 && nrOfChannels % 2 == 0) {
			SetMediaTrackInfo_Value(track, "I_NCHAN", nrOfChannels);
		}
	}

	bool getTrackSelected() {
		return GetMediaTrackInfo_Value(track, "I_SELECTED") != 0;
	}

	void setTrackSelected(bool selected) {
		SetMediaTrackInfo_Value(track, "I_SELECTED", static_cast<double>(selected));
	}

	int getTrackHeight() {
		return static_cast<int>(GetMediaTrackInfo_Value(track, "I_WNDH"));
	}

	int getFolderDepth() {
		return static_cast<int>(GetMediaTrackInfo_Value(track, "I_FOLDERDEPTH"));
	}

	// 0=normal, 1=folder parent, -1=track is last in the innermost folder, -2=track is last in innermost and next-innermost folder, -3=etc.
	void setFolderDepth(int i) {
		SetMediaTrackInfo_Value(track, "I_FOLDERDEPTH", static_cast<double>(i));
	}

	FolderCompacting getFolderCompacting() {
		static_cast<FolderCompacting>(static_cast<int>(GetMediaTrackInfo_Value(track, "I_FOLDERCOMPACT")));
	}

	void setFolderCompacting(FolderCompacting fc) {
		SetMediaTrackInfo_Value(track, "I_FOLDERCOMPACT", static_cast<double>(fc));
	}

	// bool: true=enabled, false=disabled, MidiChannel: midi channel, int: hardware output index
	std::tuple<bool, MidiChannel, int> getMidiHardwareOut() {
		int val = static_cast<int>(GetMediaTrackInfo_Value(track, "I_MIDIHWOUT"));
		if (val < 0) {
			return std::make_tuple(false, MidiChannel::omni, 0);
		} else {
			int channel = val & 0b0001'1111;
			int deviceIndex = val >> 5 & 0b0001'1111;
			return std::make_tuple(true, static_cast<MidiChannel>(channel), deviceIndex);
		}
	}

	void setMidiHardwareOut(bool enabled, MidiChannel channel = MidiChannel::omni, int deviceIndex = 0) {
		int val = -1;
		if (enabled && deviceIndex <= 31) {
			val = static_cast<int>(channel) + 32 * deviceIndex;
		}
		SetMediaTrackInfo_Value(track, "I_MIDIHWOUT", val);
	}

	void setCustomColor(int r, int g, int b) {
		SetMediaTrackInfo_Value(track, "I_CUSTOMCOLOR", static_cast<double>(ColorToNative(r, g, b) | 0x100000)); // 'or' the 21st bit
	}

	int getHeightOverride() {
		return static_cast<int>(GetMediaTrackInfo_Value(track, "I_HEIGHTOVERRIDE"));
	}

	// 0 for no override
	void setHeightOverride(int pixels) {
		if (pixels > 0 && pixels < 512) {
			SetMediaTrackInfo_Value(track, "I_HEIGHTOVERRIDE", static_cast<double>(pixels));
		}
	}

	double getVolume() {
		return GetMediaTrackInfo_Value(track, "D_VOL");
	}

	void setVolume(double volume) {
		SetMediaTrackInfo_Value(track, "D_VOL", volume);
	}

	double getPan() {
		return GetMediaTrackInfo_Value(track, "D_PAN");
	}

	void setPan(double pan) {
		if (pan < -1) {
			pan = -1;
		} else if (pan > 1) {
			pan = 1;
		}
		SetMediaTrackInfo_Value(track, "D_PAN", pan);
	}

	double getWidth() {
		return GetMediaTrackInfo_Value(track, "D_WIDTH");
	}

	void setWidth(double width) {
		if (width < -1) {
			width = -1;
		} else if (width > 1) {
			width = 1;
		}
		SetMediaTrackInfo_Value(track, "D_WIDTH", width);
	}

	double getDualPanL() {
		return GetMediaTrackInfo_Value(track, "D_DUALPANL");
	}

	void setDualPanL(double pan) {
		if (pan < -1) {
			pan = -1;
		} else if (pan > 1) {
			pan = 1;
		}
		SetMediaTrackInfo_Value(track, "D_DUALPANL", pan);
	}

	double getDualPanR() {
		return GetMediaTrackInfo_Value(track, "D_DUALPANR");
	}

	void setDualPanR(double pan) {
		if (pan < -1) {
			pan = -1;
		} else if (pan > 1) {
			pan = 1;
		}
		SetMediaTrackInfo_Value(track, "D_DUALPANR", pan);
	}

	PanMode getPanMode() {
		return static_cast<PanMode>(static_cast<int>(GetMediaTrackInfo_Value(track, "I_PANMODE")));
	}

	void setPanMode(PanMode mode) {
		SetMediaTrackInfo_Value(track, "I_PANMODE", static_cast<double>(mode));
	}

	double getPanLaw() {
		return GetMediaTrackInfo_Value(track, "D_PANLAW");
	}

	// <0: project default, 1: 0db etc
	void setPanLaw(double val) {
		SetMediaTrackInfo_Value(track, "D_PANLAW", val);
	}

	TrackEnvelope* getTrackEnvelope() {
		return reinterpret_cast<TrackEnvelope*>(static_cast<int>(GetMediaTrackInfo_Value(track, "P_ENV")));
	}

	bool getShowInMixer() {
		return GetMediaTrackInfo_Value(track, "B_SHOWINMIXER") != 0;
	}

	void setShowInMixer(bool show) {
		SetMediaTrackInfo_Value(track, "B_SHOWINMIXER", static_cast<double>(show));
	}

	bool getShowInTCP() {
		return GetMediaTrackInfo_Value(track, "B_SHOWINTCP") != 0;
	}

	void setShowInTCP(bool show) {
		SetMediaTrackInfo_Value(track, "B_SHOWINTCP", static_cast<double>(show));
	}

	bool getMainSend() {
		return GetMediaTrackInfo_Value(track, "B_MAINSEND") != 0;
	}

	void setMainSend(bool sendToParent) {
		SetMediaTrackInfo_Value(track, "B_MAINSEND", static_cast<double>(sendToParent));
	}

	int getMainSendOffset() {
		return static_cast<int>(GetMediaTrackInfo_Value(track, "C_MAINSEND_OFFS"));
	}

	void setMainSendOffset(int offset) {
		if (offset >= 0 && offset <= 62) {
			SetMediaTrackInfo_Value(track, "C_MAINSEND_OFFS", static_cast<double>(offset));
		}
	}

	bool getFreeMode() {
		return GetMediaTrackInfo_Value(track, "B_FREEMODE") != 0;
	}

	void setFreeMode(bool enabled) {
		SetMediaTrackInfo_Value(track, "B_FREEMODE", static_cast<double>(enabled));
	}

	float getFXSendScale() {
		return static_cast<float>(GetMediaTrackInfo_Value(track, "F_MCP_FXSEND_SCALE"));
	}

	void setFXSendScale(float scale) {
		if (scale < 0) {
			scale = 0;
		} else if (scale > 1) {
			scale = 1;
		}
		SetMediaTrackInfo_Value(track, "F_MCP_FXSEND_SCALE", scale);
	}

	float getSendRegionScale() {
		return static_cast<float>(GetMediaTrackInfo_Value(track, "F_MCP_SENDRGN_SCALE"));
	}

	void setSendRegionScale(float scale) {
		if (scale < 0) {
			scale = 0;
		} else if (scale > 1) {
			scale = 1;
		}
		SetMediaTrackInfo_Value(track, "F_MCP_SENDRGN_SCALE", scale);
	}

	void addFX(std::string fxname) {
		TrackFX_AddByName(track, fxname.c_str(), false, -1); // -1: always instansiate
	}
};

extern "C" 
{
	__declspec(dllexport) void start(reaper_plugin_info_t* rec) { 
		REAPERAPI_LoadAPI(rec->GetFunc);

		Undo_BeginBlock();
		PreventUIRefresh(1);

		Track t{ GetSelectedTrack2(nullptr, 0, false) };
		t.addFX("ReaEQ");

		PreventUIRefresh(-1);
		//UpdateTimeline();
		//UpdateArrange();
		Undo_EndBlock("command.dll", 0);
	}
}