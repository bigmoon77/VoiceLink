# VoiceLink Design

このドキュメントは VoiceLink のシステム構成、モジュール境界、クラス設計、データフロー、設計上の決定事項を管理します。
実装の進行に合わせて更新してください。

## 1. システム概要

VoiceLink は Windows 上で動作する音声入出力アプリケーションです。
最終的には、グローバルホットキーで音声を入力し、認識テキストを ChatGPT の Web 入力欄へ渡す機能を提供します。

初期開発では、VOICEVOX を利用した Text-to-Speech の変換・再生機能を先行して実装します。

### 1.1 初期リリースの最低限の機能要件

1. 入力したテキストを音声へ変換できる。
2. 変換した音声を再生できる。
3. GUI から VOICEVOX の話者・スタイルを選択できる。
4. GUI から読み上げ対象のテキストを入力できる。

初期 GUI は Win32 API と Windows 標準コントロールで実装します。外部 GUI フレームワークへの依存は追加しません。

- 話者選択 Drop-down: VOICEVOX Engine から取得した話者・スタイル一覧を表示する。
- Debug 用 TextBox: 読み上げ対象のテキストを入力・編集する複数行テキストボックス。
- 再生ボタン: 選択中の話者・スタイルと TextBox の内容で音声合成を実行し、生成音声を再生する。
- 状態表示: Engine 未起動、話者一覧取得失敗、音声合成失敗、再生失敗などを確認できるようにする。

話者一覧が取得できない場合、固定のダミー一覧で代用せず、接続エラーとして扱います。

### 1.2 全体構成

```text
VoiceLink (Application)
    |
    +-- main_window (Win32 GUI)
    |      +-- speaker_combo_box
    |      +-- debug_text_box
    |      +-- play_button
    |      +-- status_text
    |
    +-- tts_controller
    |      +-- GUI input / result mediation
    |      +-- asynchronous worker
    |
    +-- VoiceLink.TTS
    |      +-- tts_engine (interface)
    |      |      +-- voicevox_engine
    |      +-- VOICEVOX Engine (HTTP / localhost)
    |
    +-- VoiceLink.Audio
    |      +-- audio_player
    |
    +-- voicevox_process
    |      +-- Engine path discovery / process ownership
    +-- [Future] VoiceLink.Speech
    |      +-- audio_capture
    |      +-- speech_recognizer
    |
    +-- [Future] VoiceLink.Browser
           +-- native_messaging_host

Chrome Extension
    +-- ChatGPT tab / input detection
    +-- recognized text injection
```

## 2. プロジェクト構成案

現在は単一の Visual Studio アプリケーションプロジェクトに以下のソースを配置しています。TTS / Audio の静的ライブラリ分離は、必要になった段階で行います。

```text
VoiceLink/
├── VoiceLink.sln
├── VoiceLink.vcxproj
├── VoiceLink.vcxproj.filters
├── AGENTS.md
├── design.md
├── main.cpp
├── main_window.h / .cpp
├── tts_controller.h / .cpp
├── tts_engine.h
├── voicevox_engine.h / .cpp
└── audio_player.h / .cpp
```

TTS / Audio は現在アプリケーションプロジェクト内にあります。責務を分離したまま実装し、必要になった段階で静的ライブラリへ切り出します。
VOICEVOX Engine は別プロセスとして起動し、HTTP API で接続します。

## 3. モジュール設計

### 3.1 VoiceLink (Application)

**責務**
- アプリケーションのエントリーポイント。
- 初期 GUI の生成とイベント処理。
- 設定の読み込みと保持。
- 各モジュールの初期化・終了処理。
- ユーザー操作に応じた各機能の呼び出し。
- モジュール間の依存関係の組み立て。

アプリケーション本体に TTS エンジン固有の HTTP 処理や WAV 再生処理を実装しません。

#### `main_window`

Win32 API を利用して初期 GUI を管理するクラスです。

**表示要素**
- `speaker_combo_box`: 話者・スタイル選択。
- `debug_text_box`: 読み上げテキスト入力。
- `play_button`: 合成・再生の開始。
- `status_text`: Engine 接続状態や処理結果の表示。

**主な処理**
- `CreateWindowExW` で ComboBox、複数行 Edit、Button、Static を生成する。
- 話者一覧を ComboBox に設定する。
- 選択中の話者 ID と入力テキストを取得する。
- 再生要求と非同期処理結果をコールバック・ウィンドウメッセージで通知する。
- 状態表示と再生ボタンの有効状態を更新する。

HTTP 通信や WAV 再生の詳細は扱わず、`tts_controller` へ委譲します。

#### `tts_controller`

GUI と TTS / Audio の間を仲介するクラスです。現在はアプリケーションプロジェクト内に配置しています。

**責務**
- 起動時に VOICEVOX Engine へ接続し、話者・スタイル一覧を取得する。
- `main_window` から読み上げテキストと speaker ID を受け取る。
- `tts_engine` に音声合成を依頼する。
- 合成結果の WAV を `audio_player` へ渡す。
- 接続状態、処理中、成功、失敗を GUI に反映する。
- HTTP 通信・合成処理をワーカースレッドで実行し、完了を `PostMessageW` で UI スレッドへ通知する。

同時に複数の合成要求を実行せず、処理中は再生ボタンを無効化します。UI コントロールの操作は UI スレッド上で行います。

### 3.2 VoiceLink.TTS

**責務**
- 音声合成エンジンの共通インターフェースを提供する。
- VOICEVOX Engine との HTTP 通信を行う。
- 話者・スタイル情報を取得する。
- テキストを WAV データへ変換する。
- 音声再生デバイスや再生制御を扱わない。

#### `tts_engine`

音声合成エンジンの抽象インターフェースです。

実装済みのインターフェースは以下の通りです。

```cpp
class tts_engine
{
public:
    struct speaker_style
    {
        std::wstring speaker_name;
        std::wstring style_name;
        int speaker_id = -1;
    };

    virtual ~tts_engine() = default;

    virtual bool initialize(
        std::vector<speaker_style>& speakers,
        std::wstring& error
    ) = 0;

    virtual bool synthesize(
        const std::wstring& text,
        int speaker_id,
        std::vector<std::uint8_t>& wav_data,
        std::wstring& error
    ) = 0;
};
```

失敗時は `false` を返し、詳細を `error` に格納します。話者・スタイル一覧は共通の `speaker_style` 型で返し、VOICEVOX 固有の JSON 構造をアプリケーション本体へ公開しません。

#### `voicevox_engine`

`tts_engine` を実装し、VOICEVOX Engine の HTTP API を呼び出します。

**主な責務**
- localhost の VOICEVOX Engine (`127.0.0.1:50021`) へ WinHTTP で接続する。
- `GET /speakers` で話者・スタイル一覧を取得し、Windows.Data.Json で解析する。
- テキストを UTF-8 化・URL エンコードし、`POST /audio_query` を実行する。
- AudioQuery JSON を `POST /synthesis` へ送信する。
- 返却データが RIFF/WAVE 形式であることを確認し、WAV バイト列を返す。
- HTTP ステータス、通信エラー、JSON 解析エラーを呼び出し元へ返す。

**VOICEVOX API の処理順**

1. `GET /speakers` — 話者とスタイルの一覧を取得。
2. `POST /audio_query?text=...&speaker=...` — テキストから AudioQuery を生成。
3. `POST /synthesis?speaker=...` — AudioQuery の JSON を送信して WAV を取得。

接続先の初期値は `http://127.0.0.1:50021` とします。
ポート番号や URL は将来の設定変更に備えて固定値を各所へ散在させず、一か所で管理します。

VOICEVOX Engine の起動・終了は voicevox_process が管理します。標準インストール先と新しいエンジン管理フォルダーから run.exe を検索し、未起動の場合だけ起動します。VoiceLink が起動したプロセスは Job Object で所有し、VoiceLink 終了時に停止します。既存の Engine には接続するだけで、終了処理の対象にしません。

### 3.3 VoiceLink.Audio

**責務**
- WAV データを受け取り、音声として再生する。
- 再生停止を行う。
- TTS エンジンや HTTP 通信に依存しない。

#### `audio_player`

現時点では WinMM の `PlaySoundW` を利用してメモリ上の WAV を再生します。

- `play`: RIFF/WAVE ヘッダーを確認して非同期再生する。
- `stop`: 再生を停止する。
- 非同期再生中にデータが解放されないよう、WAV バッファをメンバーで保持する。

初期の動作確認を優先した実装です。将来、再生状態の取得や細かな制御が必要になった段階で XAudio2 等への置き換えを検討します。

Audio は TTS の HTTP 通信や JSON 解析へ依存しません。

## 4. 初期データフロー

### 4.1 起動・話者一覧取得

```text
main_window
 |
 | initialize_async
 v
tts_controller
 |
 | initialize
 v
voicevox_engine
 |
 | GET /speakers
 v
VOICEVOX Engine
 |
 | speaker/style list
 v
voicevox_engine
 |
 | common speaker/style data
 v
tts_controller
 |
 | PostMessage result
 v
main_window
 |
 | populate
 v
speaker_combo_box
```

### 4.2 テキスト読み上げ

```text
User
 |
 | select speaker + enter text + click Play
 v
main_window
 |
 | input text + speaker_id
 v
tts_controller
 |
 | synthesize(text, speaker_id)
 v
tts_engine / voicevox_engine
 |
 | 1. POST /audio_query
 | 2. POST /synthesis
 v
VOICEVOX Engine
 |
 | WAV bytes
 v
voicevox_engine
 |
 | WAV bytes
 v
tts_controller
 |
 | play(wav_data)
 v
audio_player
 |
 v
Audio Output Device
```

## 5. 依存関係のルール

| 呼び出し元 | 依存先 | 方針 |
|---|---|---|
| VoiceLink / main_window | tts_controller | GUI の入力と非同期結果を仲介する |
| tts_controller | tts_engine | 抽象インターフェース経由で合成を依頼する |
| tts_controller | audio_player | WAV データを渡して再生を依頼する |
| VoiceLink.TTS | HTTP クライアント / JSON ライブラリ | VOICEVOX 通信の内部実装として利用する |
| VoiceLink.Audio | 再生バックエンド | Audio 内部に閉じ込める |
| VoiceLink.Audio | VoiceLink.TTS | 依存させない |
| VoiceLink.TTS | VoiceLink.Audio | 依存させない |

モジュール間の依存は原則として一方向にします。循環依存を作らないでください。

## 6. 将来機能の設計方針

### 6.1 音声入力・音声認識

将来の `VoiceLink.Speech` は、少なくとも以下の責務に分けます。

- `audio_capture`: マイク入力の取得と録音状態の管理。
- `speech_recognizer`: 音声データをテキストへ変換する。
- 音声認識エンジン固有の通信・推論処理は、認識モジュール内に閉じ込める。

録音制御と認識処理の API は、実際に採用する認識エンジンを決定した段階で設計します。

### 6.2 ChatGPT Web 連携

ブラウザー連携は、C++ アプリケーションと Chrome 拡張機能の間を Native Messaging で接続する構成を基本案とします。

- C++ 側: Native Messaging Host としてメッセージを受け渡す。
- 拡張機能側: ChatGPT の対象タブと入力欄を検出し、テキストを反映する。
- 音声認識・TTS の各モジュールは、ブラウザー DOM を直接操作しない。

フォーカスを奪わずに入力することを重要な要件とします。実現可能なブラウザー API と ChatGPT Web 側の挙動を検証しながら詳細を確定します。

## 7. エラー処理方針

初期実装で少なくとも次の失敗を区別します。

- VOICEVOX Engine に接続できない。
- HTTP リクエストが失敗する、またはタイムアウトする。
- Engine がエラー応答を返す。
- JSON の解析に失敗する。
- 指定した話者・スタイルが存在しない。
- WAV データが空、または不正である。
- 音声デバイスの初期化・再生に失敗する。

エラーの詳細を保持し、アプリケーション側でログ表示やユーザー通知を行える設計にします。

## 8. 未決定事項

以下は実装前または実装時に検証して決定します。

| 項目 | 現状 | 決定時の観点 |
|---|---|---|
| GUI フレームワーク | **決定: Win32 標準コントロール** | Windows API の ComboBox / Edit / Button / Static を使用。追加の GUI 依存なし |
| C++ 言語標準 | **決定: C++17** | C++/WinRT を利用するため |
| HTTP クライアント | **決定: WinHTTP** | Windows 標準 API を利用 |
| JSON パーサー | **決定: Windows.Data.Json (C++/WinRT)** | Windows SDK の JSON API を利用 |
| 音声再生 API | **初期実装: WinMM PlaySoundW** | WAV メモリ再生。高度な制御が必要になれば XAudio2 等を検討 |
| TTS の戻り値形式 | `bool` + WAV バイト列 + エラー文字列 | `tts_engine` の共通インターフェースで統一 |
| 話者・スタイル型 | `tts_engine::speaker_style` | 話者名、スタイル名、speaker ID を保持 |
| 設定保存形式 | 未確定 | 接続先、話者 ID、音量などの保存 |
| VOICEVOX Engine の管理 | **決定: voicevox_process で自動管理** | 標準パスと `%APPDATA%\\voicevox\\vvpp-engines` を検索。既存 Engine は再利用し、自分で起動したプロセスだけ終了時に停止 |

## 9. 現在の実装状況

- Win32 のメインウィンドウと標準コントロールを実装済み。
- `tts_engine` 抽象インターフェースと VOICEVOX 実装を追加済み。
- WinHTTP で `/speakers`、`/audio_query`、`/synthesis` を呼び出す。
- `tts_controller` が GUI 入力、非同期 TTS 処理、結果通知、音声再生を仲介する。
- `audio_player` が WAV をメモリから非同期再生する。
- 接続先は現在 `http://127.0.0.1:50021` 固定。`voicevox_process` が Engine の検索・起動・終了管理を行う。
- 各ソースはアプリケーションプロジェクトに含めている。TTS / Audio の静的ライブラリ分離は後続作業とする。
- Windows / MSVC でのビルド・実機接続テストは未確認。

## 10. 更新ルール

- クラスを追加・削除した場合は、該当モジュールの設計を更新する。
- 公開インターフェースを変更した場合は、クラス設計とデータフローを更新する。
- モジュール間の依存関係を変更した場合は、依存関係表を更新する。
- 未決定事項が確定した場合は、決定内容と必要に応じて理由を記録する。
- 実装前の案と実装済みの仕様を混同しない。
