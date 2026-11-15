#pragma once
// Shoal friendly mode: the current app's settings as a list a mouse can
// drive. shoal::Core builds a FriendlyModel on request (Core::friendlyModel)
// and carries out FriendlyEdits (Core::friendlyEdit) by putting the
// firmware's menu cursor on the row and sending the app's own encoder and
// button handlers the events a person would have made, so the app's guards
// and follow-ups run. Rack-free, C++11-compatible, no firmware header.
//
// Every pointer in a model (labels, value names, tab labels) points to static
// storage, so a model may be copied to another thread and read there.
#include <cstdint>

namespace shoal {
constexpr int kMaxRows = 72, kMaxTabs = 4, kRowText = 12, kStatusText = 24;
enum class RowKind : uint8_t {
	Number,     // slider: value in [min, max]
	Choice,     // dropdown: names[value - min], nameCount entries
	Toggle,     // on/off: value 0 or 1
	Mask,       // toggle cells: value is the bit mask, max is the number of cells (0: none)
	Action,     // a button: Press does it; value unused
	SubEditor,  // edited in hardware mode only
	Heading     // a section title, no control
};
enum : uint8_t {
	kRowCursor = 1,   // the firmware's menu cursor is on the row
	kRowEditing = 2,  // ... and editing its value
	kRowExtra = 4,    // not a setting of the table (id >= 256)
	kRowConfirm = 8   // an Action that undoes work (every setting, the grid): the list asks before it posts the Press
};
struct Row {
	RowKind kind;
	uint8_t flags;
	int16_t id;                  // stable for the app: setting index, or >= 256 for an extra row
	const char* label;           // static storage
	int32_t value, min, max, def;
	// Choice; static storage. nameCount can be less than max - min + 1 (the
	// firmware has a few name arrays shorter than their range): never read
	// names[i] for i >= nameCount. A row whose value has no name carries the
	// number in `text`.
	const char* const* names;
	int16_t nameCount;
	char text[kRowText];         // the value as the firmware prints it; empty: show the number or the name
};
enum : uint8_t {
	kModelBusy = 1,       // the app is not on its settings menu (app menu or a sub-editor open): every edit is dropped
	kModelTruncated = 2   // more rows than kMaxRows: a fault in an adapter, never expected
};
struct FriendlyModel {
	uint8_t app;                 // index into Core::appName
	uint8_t tabs, tab;           // channels and the firmware's selected one; tabs <= 1: no tab bar
	uint8_t flags;               // kModelBusy, kModelTruncated
	const char* tabLabel[kMaxTabs];
	char status[kStatusText];    // the title bar's live text (freeze, clock, frequency); may be empty
	int16_t rows;
	Row row[kMaxRows];
};
struct FriendlyEdit {
	enum Type : uint8_t { SetValue, ToggleBit, Press, SelectTab, Focus };
	Type type;
	uint8_t app;                 // the app the UI drew; an edit for another app is dropped
	uint8_t tab;                 // the tab the row belongs to
	int16_t id;                  // Row::id; unused for SelectTab
	int32_t value;               // SetValue: target; ToggleBit: bit number; SelectTab: tab
};
}
