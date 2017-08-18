#include "stdafx.h"

#define REAPERAPI_IMPLEMENT

#include "reaper_plugin.h" 
#include "reaper_plugin_functions.h"



// 1: In project properties/General/Windows SDK Version: change the Windows SDK Version to your version.

// 2: In project properties/Build Events/Post-Build Event: change the path to where "extension" expects the dll.
// It should be named command.dll, for now!

// 3: (optional) If you need simple logging, change the log.txt path
std::string logpath = "C:/Users/User/Desktop/log.txt";


std::default_random_engine generator;
std::uniform_real_distribution<double> distribution(0.0, 1.0);


void log(std::string str) {
	std::ofstream outfile;
	outfile.open(logpath.c_str(), std::ios::app);
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
enum class ChannelMode { normal=0, reverseStereo, downmix, left, right };
enum class SOLO { not_soloed, solo, soloed_in_place };
enum class SourceType { wave, midi };

class Input {
public:
	InputType type = InputType::none;
	int audioChannel = 0;
	bool isStereo = false;
	MidiChannel midiChannel = MidiChannel::omni;
	MidiHardware midiHardware = MidiHardware::all;
};


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

class PCMSource {
	PCM_source* source;
	static constexpr int bufsize = 256;
	char buf[bufsize];

public:
	PCMSource(PCM_source* src) {
		source = src;
	}

	PCMSource(std::string path) {
		source = PCM_Source_CreateFromFileEx(path.c_str(), false);
	}

	PCMSource(SourceType type) {
		if (type == SourceType::midi) {
			source = PCM_Source_CreateFromType("MIDI");
		} else if (type == SourceType::wave) {
			source = PCM_Source_CreateFromType("WAVE");
		}
	}

	PCM_source* getSource() {
		return source;
	}

	// in-project MIDI source has no file name
	std::string getFilename() {
		GetMediaSourceFileName(source, buf, bufsize);
		return std::string{ buf };
	}

	double getLength() {
		bool qn;
		return GetMediaSourceLength(source, &qn);
	}

	int getNumChannels() {
		return static_cast<int>(GetMediaSourceNumChannels(source));
	}

	PCMSource getParentSource() {
		return PCMSource{ GetMediaSourceParent(source) };
	}

	int getSampleRate() {
		return GetMediaSourceSampleRate(source);
	}

	SourceType getSourceType() {
		GetMediaSourceType(source, buf, bufsize);
		std::string type{ buf };
		if (type.compare("WAV")) {
			return SourceType::wave;
		} else if (type.compare("MIDI")) {
			return SourceType::midi;
		}
	}
};

class Take {
	MediaItem_Take* take;
	char buf[128];

public:
	Take(MediaItem_Take* t) {
		take = t;
	}

	MediaItem_Take* getTake() {
		return take;
	}

	std::string getName() {
		auto name = GetTakeName(take);
		return std::string{ name };
	}

	double getVolume() {
		return GetMediaItemTakeInfo_Value(take, "D_VOL");
	}

	void setVolume(double volume) {
		SetMediaItemTakeInfo_Value(take, "D_VOL", volume);
	}

	double getPan() {
		return GetMediaItemTakeInfo_Value(take, "D_PAN");
	}

	void setPan(double pan) {
		SetMediaItemTakeInfo_Value(take, "D_PAN", pan);
	}

	double getPanLaw() {
		return GetMediaItemTakeInfo_Value(take, "D_PANLAW");
	}

	void setPanLaw(double panLaw) {
		SetMediaItemTakeInfo_Value(take, "D_`PANLAW", panLaw);
	}

	double getPlayRate() {
		return GetMediaItemTakeInfo_Value(take, "D_PLAYRATE");
	}

	void setPlayRate(double playRate) {
		SetMediaItemTakeInfo_Value(take, "D_PLAYRATE", playRate);
	}

	double getPitch() {
		return GetMediaItemTakeInfo_Value(take, "D_PITCH");
	}

	void setPitch(double pitch) {
		SetMediaItemTakeInfo_Value(take, "D_PITCH", pitch);
	}

	double getStartOffset() {
		return GetMediaItemTakeInfo_Value(take, "D_STARTOFFS");
	}

	void setStartOffset(double offset) {
		SetMediaItemTakeInfo_Value(take, "D_STARTOFFS", offset);
	}

	bool getPreservePitch() {
		return GetMediaItemTakeInfo_Value(take, "B_PPITCH") != 0;
	}

	// preserve pitch when changing rate
	void setPreservePitch(bool preservePitch) {
		SetMediaItemTakeInfo_Value(take, "B_PPITCH", static_cast<double>(preservePitch));
	}

	ChannelMode getChannelMode() {
		return static_cast<ChannelMode>(static_cast<int>(GetMediaItemTakeInfo_Value(take, "I_CHANMODE")));
	}

	void setChannelMode(ChannelMode mode) {
		SetMediaItemTakeInfo_Value(take, "I_CHANMODE", static_cast<double>(mode));
	}

	// probe the return values to create an enum class directly rather than doing bit arithmetic
	int getPitchShifterMode() {
		static_cast<int>(GetMediaItemTakeInfo_Value(take, "I_PITCHMODE"));
	}

	void setFadeOutLength(double length) {
		SetMediaItemTakeInfo_Value(take, "D_FADEOUTLEN", length);
	}

	std::tuple<int, int, int> getCustomColor() {
		int color = static_cast<int>(GetMediaItemTakeInfo_Value(take, "I_CUSTOMCOLOR"));
		int r, g, b;
		ColorFromNative(color, &r, &g, &b);
		return std::make_tuple(r, g, b);
	}

	void setCustomColor(int r, int g, int b) {
		SetMediaItemTakeInfo_Value(take, "I_CUSTOMCOLOR", static_cast<double>(ColorToNative(r, g, b) | 0x1000000)); // 'or' the 21st bit
	}

	int getTakeNumber() {
		return static_cast<int>(GetMediaItemTakeInfo_Value(take, "IP_TAKENUMBER"));
	}

	// declare Item!
	/*Item getParentItem() {
		return Item{ GetMediaItemTake_Item(take) };
	}*/

	// declare Track!
	/*Track getParentTrack() {
		return Track{ GetMediaItemTake_Track(take) };
	}*/


	PCMSource getSource() {
		return PCMSource{ GetMediaItemTake_Source(take) };
	}

	void setSource(PCMSource source) {
		SetMediaItemTake_Source(take, source.getSource());
	}
};

class Item {
	MediaItem* item;
	char buf[128];

public:
	Item(MediaItem* i) {
		item = i;
	}

	MediaItem* getItem() {
		return item;
	}

	bool getMute() {
		return GetMediaItemInfo_Value(item, "B_MUTE") != 0;
	}

	void setMute(bool mute) {
		SetMediaItemInfo_Value(item, "B_MUTE", static_cast<double>(mute));
	}

	bool getLock() {
		return GetMediaItemInfo_Value(item, "C_LOCK") != 0;
	}

	void setLock(bool lock) {
		SetMediaItemInfo_Value(item, "C_LOCK", static_cast<double>(lock));
	}

	bool getLoopSource() {
		return GetMediaItemInfo_Value(item, "B_LOOPSRC") != 0;
	}

	void setLoopSource(bool loopSource) {
		SetMediaItemInfo_Value(item, "B_LOOPSRC", static_cast<double>(loopSource));
	}

	bool getPlayAllTakes() {
		return GetMediaItemInfo_Value(item, "B_ALLTAKESPLAY") != 0;
	}

	void setPlayAllTakes(bool allTakes) {
		SetMediaItemInfo_Value(item, "B_ALLTAKESPLAY", static_cast<double>(allTakes));
	}

	bool getSelected() {
		return GetMediaItemInfo_Value(item, "B_UISEL") != 0;
	}

	void setSelected(bool selected) {
		SetMediaItemInfo_Value(item, "B_UISEL", static_cast<double>(selected));
	}

	double getVolume() {
		return GetMediaItemInfo_Value(item, "D_VOL");
	}

	void setVolume(double volume) {
		SetMediaItemInfo_Value(item, "D_VOL", volume);
	}

	double getPosition() {
		return GetMediaItemInfo_Value(item, "D_POSITION");
	}

	void setPosition(double position) {
		SetMediaItemInfo_Value(item, "D_POSITION", position);
	}

	double getSnapOffset() {
		return GetMediaItemInfo_Value(item, "D_SNAPOFFSET");
	}

	void setSnapOffset(double offset) {
		SetMediaItemInfo_Value(item, "D_SNAPOFFSET", offset);
	}

	double getLength() {
		return GetMediaItemInfo_Value(item, "D_LENGTH");
	}

	void setLength(double length) {
		SetMediaItemInfo_Value(item, "D_LENGTH", length);
	}

	double getFadeInLength() {
		return GetMediaItemInfo_Value(item, "D_FADEINLEN");
	}

	void setFadeInLength(double length) {
		SetMediaItemInfo_Value(item, "D_FADEINLEN", length);
	}

	double getFadeOutLength() {
		return GetMediaItemInfo_Value(item, "D_FADEOUTLEN");
	}

	void setFadeOutLength(double length) {
		SetMediaItemInfo_Value(item, "D_FADEOUTLEN", length);
	}

	std::tuple<int, int, int> getCustomColor() {
		int color = static_cast<int>(GetMediaItemInfo_Value(item, "I_CUSTOMCOLOR"));
		int r, g, b;
		ColorFromNative(color, &r, &g, &b);
		return std::make_tuple(r, g, b);
	}

	void setCustomColor(int r, int g, int b) {
		SetMediaItemInfo_Value(item, "I_CUSTOMCOLOR", static_cast<double>(ColorToNative(r, g, b) | 0x1000000)); // 'or' the 21st bit
	}

	int getGroupID() {
		return static_cast<int>(GetMediaItemInfo_Value(item, "I_GROUPID"));
	}

	void setGroupID(int id) {
		SetMediaItemInfo_Value(item, "I_GROUPID", static_cast<double>(id));
	}

	int getCurrentTake() {
		return static_cast<int>(GetMediaItemInfo_Value(item, "I_CURTAKE"));
	}

	void setCurrentTake(int takeNumber) {
		SetMediaItemInfo_Value(item, "I_CURTAKE", static_cast<double>(takeNumber));
	}

	// gets item number within track
	int getItemNumber() {
		return static_cast<int>(GetMediaItemInfo_Value(item, "IP_ITEMNUMBER"));
	}

	int getNumberOfTakes() {
		return static_cast<int>(GetMediaItemNumTakes(item));
	}

	Take getTake(int takeNumber) {
		return Take{ GetMediaItemTake(item, takeNumber) };
	}

	// declare Track!
	/*Track getParentTrack() {
	return Track{ GetMediaItemTrack(item) };
	}*/
};

class TrackEnvelope {

};

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

	Track getParentTrack() {
		return Track{ GetParentTrack(track) };
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

	std::tuple<int, int, int> getCustomColor() {
		int color = static_cast<int>(GetMediaTrackInfo_Value(track, "I_CUSTOMCOLOR"));
		int r, g, b;
		ColorFromNative(color, &r, &g, &b);
		return std::make_tuple(r, g, b);
	}

	void setCustomColor(int r, int g, int b) {
		SetMediaTrackInfo_Value(track, "I_CUSTOMCOLOR", static_cast<double>(ColorToNative(r, g, b) | 0x1000000)); // 'or' the 21st bit
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

	void armMidiInput() {
		Input input;
		input.type = InputType::MIDI;
		input.midiHardware = MidiHardware::hw3; // my midi controller lives here.
		setRecordInput(input);
		setRecordMonitor(RecordMonitor::normal);
		setRecordArmed(true);
	}
};

void filterTrackList(std::function<bool(Track)> compareFunc) {
	for (int i = 0, trackCount = CountTracks(nullptr); i < trackCount; i++) {
		Track t{ i };
		if (compareFunc(t)) {
			t.setShowInTCP(true);
		} else {
			t.setShowInTCP(false);
		}
	}

}

void filterMixer(std::function<bool(Track)> compareFunc) {
	for (int i = 0, trackCount = CountTracks(nullptr); i < trackCount; i++) {
		Track t{ i };
		if (compareFunc(t)) {
			t.setShowInMixer(true);
		} else {
			t.setShowInMixer(false);
		}
	}
}

/*
This is a good starting point for entering API commands.
No need to call API functions like undo/preventUI/update/tracklist_adjust, as the enclosing code takes care of that.
*/
void command() {
	Track{ GetSelectedTrack2(nullptr, 0, false) }.setCustomColor(128, 64, 0);
}

extern "C" 
{
	__declspec(dllexport) void start(reaper_plugin_info_t* rec) { 
		REAPERAPI_LoadAPI(rec->GetFunc);

		Undo_BeginBlock();
		PreventUIRefresh(1);


		command();


		TrackList_AdjustWindows(false);
		UpdateArrange();
		UpdateTimeline();

		PreventUIRefresh(-1);
		Undo_EndBlock("command.dll", 0);
	}
}