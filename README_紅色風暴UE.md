# 紅色風暴 Red Storm — Unreal Engine 5 版

由 Godot 版（RedStorm/，約 1.1 萬行 GDScript）完整改寫為 **UE 5.4 C++ 專案**（5.5～5.7 也可開啟並升級）。
主題「混合戰爭」：11 國專屬單位與國家特性、外交（結盟／停火／盟友支援）、間諜滲透、RSN 時事事件、
基礎＋混合戰爭共 11 個戰役任務與新聞式簡報、存讀檔、戰績與 20 項成就。

## 一、第一次使用

1. 安裝 **Visual Studio 2022**（免費 Community 版）：雙擊上層資料夾的 `安裝_VisualStudio2022_UE編譯環境.bat`。
   （VS Code 不夠，Unreal 需要 VS 2022 裡的 MSVC 編譯器。）
2. 雙擊 `編譯_RedStorm_UE.bat`（第一次 5～15 分鐘）。失敗時錯誤會寫在 `build_log.txt`，交給 Claude 修正即可。
3. 雙擊 `直接遊玩_RedStorm.bat` 開始玩；或 `開啟_RedStorm_編輯器.bat` 進入 Unreal 編輯器（按「播放」即可測試）。
   也可以直接雙擊 `RedStorm.uproject`，看到「是否重新建置模組」時選「是」。
4. `自動測試_RedStorm.bat`：自動編譯，然後無人操作跑一輪（片頭 → 選單 → 4 倍速遭遇戰 → 存檔/讀檔 → 任務一簡報與開場），
   截圖存到 `Saved/AutoTest/`，紀錄在 `Saved/Logs/RedStorm.log`（搜尋 AUTOTEST）。
5. `外觀展示_RedStorm.bat`：把全部建築與（非艦艇）單位擺在地圖上，自動拍 12 張外觀截圖到 `Saved/AutoTest/`（g00～g11）。

> 編譯環境備註：UE 5.4 搭配 VS 2022 的 MSVC 14.44（非官方首選 14.38）。為相容已在 Target.cs 加入
> `/wd4668 /wd4067` 與強制引入 `Source/RSMsvcCompat.h`，並關閉 PCH；路徑含中文，所以 bat 會用 `subst R:` 對應資料夾。

## 二、資料夾結構

| 路徑 | 內容 |
|---|---|
| `Source/RedStorm/` | 全部 C++ 原始碼（RS*.cpp / .h） |
| `Content/RedStorm/Data/*.json` | **數值與內容表**：單位、建築、武器、國家、超武、地圖、任務、事件、成就、片頭字幕 |
| `Content/RedStorm/Audio/` | 音效、語音、音樂（WAV，執行時直接讀取） |
| `Content/RedStorm/Cameos/` | 建造欄兵種圖示（PNG） |
| `Content/RedStorm/Video/` | 片頭影片與選單背景影片（MP4） |
| `Saved/RedStorm/` | 設定、戰績、存檔（自動產生） |

## 三、不用寫程式就能修改

- **調平衡**：用記事本開 `Content/RedStorm/Data/defs.json`（造價 cost、生命 hp、速度 speed、武器 dmg/rof/range、國家加成…），存檔後重開遊戲即生效。
- **改任務文字 / 新聞標題**：`missions.json`；**改時事事件**：`events.json`；**地圖預設**：`maps.json`。
- **換成自己的 3D 模型**：在編輯器匯入模型（例如 `godot_project/custom_models/t90.glb`），
  然後到「編輯 → 專案設定 → Game → Red Storm 視覺 → Mesh Overrides」，新增 `s_tank` → 選你的模型，
  可另外設定縮放與偏航。沒設定的單位會使用程式產生的原創幾何外觀。
- **換材質**：同一頁的 Base Material（需要一個名為 `Color` 的向量參數）。
- **中文字型**：預設讀取 Windows 的微軟正黑體；要換字型可把 `NotoSansTC-Regular.ttf` / `NotoSansTC-Bold.ttf` 放進 `Content/RedStorm/Fonts/`。

## 四、操作（與 Godot 版相同）

左鍵選取／框選、右鍵移動攻擊、A 攻擊移動、S 停止、D 展開基地車、Ctrl+數字編隊、
Z 出售、X 修理、T 外交、F1 說明、F5/F9 快速存讀檔、滾輪縮放、中鍵拖曳平移、Esc 選單。

## 五、原創素材原則

- 全部素材為原創；不使用 EA 商標或《紅色警戒》專有名稱。
- 改寫時把幾個過於接近既有作品的**顯示名稱**換成原創名稱（內部 id 不變，可在 defs.json 改回或再改）：
  盟軍→西方聯盟／聯盟軍、蘇軍→東方集團／集團軍、動員兵→義務兵、猛獁坦克→巨熊重坦克、
  特斯拉線圈→電弧防禦塔、氣象控制器→氣候干預站、閃電風暴→雷暴打擊、無畏級戰艦→飛彈戰艦、
  武裝採礦車→重裝採礦車、作戰實驗室→國防研究院、空軍指揮部→航空作戰中心。
- 新聞台為原創的「RSN」。

## 六、與 Godot 版的差異

- 畫面改為**真 3D**：地形高度場（高地、坡道、懸崖、水面、橋）、3D 單位與建築、即時光影、Lumen 全域光照、爆炸閃光。
- 單位外觀由程式以幾何體組成（原創），可逐一替換為正式模型。
- 片頭改為播放 `redstorm_intro_cinematic.mp4` 並顯示中英字幕；Godot 版的系統語音朗讀（TTS）未移植。
- 遊戲邏輯（經濟、生產、AI、外交、間諜、事件、任務、存檔、成就）逐行對照移植。
