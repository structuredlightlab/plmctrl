#pragma once


namespace PLM {

	void ImagescPLM(const char* title, 
		uint8_t* data, 

		ID3D11ShaderResourceView* data_texture_view,
		ID3D11Device* g_pd3dDevice,
		ID3D11DeviceContext* g_pd3dDeviceContext,

		ImGuiIO& io, int N, int M, 
		std::mutex* mutex,
		int x0 = 0, int y0 = 0) {

		static int imgWidth = N / 4, imgHeight = M / 4;

		static ImGuiWindowFlags slm_window_flags = ImGuiWindowFlags_None;
		const ImGuiViewport* viewport = ImGui::GetMainViewport();

		static bool p_open = true;

		slm_window_flags |= ImGuiWindowFlags_NoDecoration
			| ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoTitleBar
			| ImGuiWindowFlags_NoMouseInputs
			| ImGuiWindowFlags_NoNavInputs
			| ImGuiWindowFlags_NoNavFocus
			| ImGuiWindowFlags_NoScrollbar
			| ImGuiWindowFlags_NoScrollWithMouse;

		ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2((float)N, (float)M), ImGuiCond_Always);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	

		ImGui::Begin(title, &p_open, slm_window_flags);
		ImGui::PopStyleVar(2);

		if (mutex->try_lock()) {
			// Update texture data
			D3D11_MAPPED_SUBRESOURCE mapped_resource;
			ID3D11Texture2D* pTexture = NULL;
			data_texture_view->GetResource((ID3D11Resource**)&pTexture);

			g_pd3dDeviceContext->Map(pTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource);

			// Calculate bytes per row in the input data (only RGB channels, so 3 bytes per pixel)
			size_t bytes_per_row = N * 3;

			for (size_t row = 0; row < M; ++row) {
				// Destination pointer (considering row pitch)
				uint8_t* dest = (uint8_t*)mapped_resource.pData + row * mapped_resource.RowPitch;

				// Source pointer
				uint8_t* src = (uint8_t*)data + row * bytes_per_row;

				// Manually copy RGB channels and set Alpha to 0
				for (size_t col = 0; col < N; ++col) {
					dest[col * 4 + 0] = src[col * 3 + 0]; // R
					dest[col * 4 + 1] = src[col * 3 + 1]; // G
					dest[col * 4 + 2] = src[col * 3 + 2]; // B
					dest[col * 4 + 3] = 255;              // A (set to 255)
				}
			}

			g_pd3dDeviceContext->Unmap(pTexture, 0);
			pTexture->Release();
			mutex->unlock();
		}

		static ImVec2 ulim = ImVec2(0.0f, 1.0f);
		static ImVec2 vlim = ImVec2(0.0f, 1.0f);

		int img_width = imgWidth, img_height = imgHeight;

		img_width = N;
		img_height = M;

		ImGui::Image((void*)data_texture_view, ImVec2((float)img_width, (float)img_height), ImVec2(ulim.x, vlim.x), ImVec2(ulim.y, vlim.y));

		ImGui::End();
	}


}
