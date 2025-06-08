#pragma once
#include <windows.h>

#define clamp(x, min, max) (x < min ? min : (x > max ? max : x))

// Struct to hold the index and the monitor RECT together
struct MonitorData {
	int monitorIndex;
	RECT monitorRect;
};

//////////////////// DirectX helpers ////////////////////

// Function to find the second monitor
bool GetSecondMonitorRect(RECT& rect, int monitor_id)
{
	MonitorData data = { 0, {0, 0, 0, 0} };
	bool foundSecondMonitor = false;

	EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) -> BOOL
		{
			MonitorData* monitorData = reinterpret_cast<MonitorData*>(dwData);
			MONITORINFO monitorInfo;
			monitorInfo.cbSize = sizeof(MONITORINFO);
			if (GetMonitorInfo(hMonitor, &monitorInfo))
			{
				if (monitorData->monitorIndex == 2)  // We're looking for the second monitor (index 1)
				{
					monitorData->monitorRect = monitorInfo.rcMonitor;
					return FALSE;  // Stop enumeration after finding the second monitor
				}
				monitorData->monitorIndex++;
			}
			return TRUE;  // Continue enumeration
		}, reinterpret_cast<LPARAM>(&data));

	if (data.monitorIndex == 2)  // Ensure that the second monitor was found
	{
		rect = data.monitorRect;
		foundSecondMonitor = true;
	}
	return foundSecondMonitor;
}

//////////////////// ImGui Helpers ////////////////////
void Status(bool indicator, bool same_line = true) {
	const ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoPicker |
		ImGuiColorEditFlags_NoOptions |
		ImGuiColorEditFlags_NoInputs |
		ImGuiColorEditFlags_NoTooltip |
		ImGuiColorEditFlags_NoLabel |
		ImGuiColorEditFlags_NoSidePreview |
		ImGuiColorEditFlags_NoDragDrop;
	static float green[3] = { 0.0f, 1.0f, 0.0f };
	static float red[3] = { 1.0f, 0.0f, 0.0f };
	ImVec2 pos = ImGui::GetCursorScreenPos();
	ImGui::ColorEdit3("status##", indicator ? green : red, flags);
	//ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, pos.y + 2), ImVec2(pos.x + 10, pos.y + 12), indicator ? green : red);
	//ImGui::Text(" ");
	if (same_line) ImGui::SameLine();
};

void Bit(bool indicator, bool same_line = false) {
	ImU32 green = IM_COL32(0, 255, 0, 255);
	ImU32 red = IM_COL32(255, 0, 0, 255);
	ImU32 blue = IM_COL32(0, 0, 255, 255);
	ImU32 grey = IM_COL32(120, 120, 120, 255);
	ImU32 yellow = IM_COL32(255, 255, 0, 255);
	ImVec2 pos = ImGui::GetCursorScreenPos();
	ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, pos.y + 2), ImVec2(pos.x + 10, pos.y + 12), indicator ? yellow : grey);
	ImGui::Text(" ");
	if (same_line) ImGui::SameLine();
}

void BitGreen(bool indicator, bool same_line = false) {
	ImU32 green = IM_COL32(0, 255, 0, 255);
	ImU32 red = IM_COL32(255, 0, 0, 255);
	ImU32 blue = IM_COL32(0, 0, 255, 255);
	ImU32 grey = IM_COL32(120, 120, 120, 255);
	ImU32 yellow = IM_COL32(255, 255, 0, 255);
	ImVec2 pos = ImGui::GetCursorScreenPos();
	ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, pos.y + 2), ImVec2(pos.x + 10, pos.y + 12), indicator ? green : grey);
	ImGui::Text(" ");
	if (same_line) ImGui::SameLine();
}

void ContinuousStatus(float indicator, bool same_line = true) {
	const ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoPicker |
		ImGuiColorEditFlags_NoOptions |
		ImGuiColorEditFlags_NoInputs |
		ImGuiColorEditFlags_NoTooltip |
		ImGuiColorEditFlags_NoLabel |
		ImGuiColorEditFlags_NoSidePreview |
		ImGuiColorEditFlags_NoDragDrop;
	float blue[3] = { 0.0f, 0.0f, indicator };
	ImVec2 pos = ImGui::GetCursorScreenPos();
	ImGui::ColorEdit3("status##", blue, flags);
	//ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, pos.y + 2), ImVec2(pos.x + 10, pos.y + 12), indicator ? green : red);
	//ImGui::Text(" ");
	if (same_line) ImGui::SameLine();
};