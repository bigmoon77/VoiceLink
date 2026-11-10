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
    +-- main_window (GUI)
    |      +-- speaker_combo_box
    |      +-- debug_text_box
    |      +-- play_button
    |      +-- status_text
    |
    +-- VoiceLink.TTS
    |      +-- tts_engine (interface)
    |      |      +-- voicevox_engine
    |      +-- VOICEVOX Engine (HTTP / localhost)
    |
    +-- VoiceLink.Audio
    |      +-- audio_player
    |
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

初期段階では、Visual Studio のソリューション内に以下のプロジェクトを配置します。

```text
VoiceLink/
├── VoiceLink.sln
├── AGENTS.md
├── design.md
├── main.cpp                   # Windows GUI entry point
├── main_window.h               # Win32 main window interface
├── main_window.cpp             # Win32 main window implementation
├── VoiceLink.TTS/             # 音声合成ライブラリ (static LIB)
│   ├── tts_engine.h
│   ├── voicevox_engine.h
│   └── voicevox_engine.cpp
├── VoiceLink.Audio/           # 音声再生ライブラリ (static LIB)
│   ├── audio_player.h
│   └── audio_player.cpp
├── VoiceLink.Common/          # 共通型・ユーティリティ (必要になった段階で追加)
├── VoiceLink.Speech/          # 音声入力・認識 (将来)
├── VoiceLink.Browser/         # Native Messaging Host (将来)
└── ChromeExtension/           # Chrome 拡張機能 (将来)
```

各ライブラリは、独立した配布・動的ロードが必要になるまでは静的ライブラリとして扱います。
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
- 起動時に TTS Engine から話者・スタイル一覧を取得し、Drop-down に設定する。
- 選択中の話者 ID を保持する。
- 再生ボタン押下時に TextBox の内容と話者 ID を取得する。
- TTS へ合成を依頼し、成功した WAV データを Audio へ渡す。
- 処理結果やエラーを状態表示へ反映する。

GUI のイベントハンドラーに HTTP 通信や WAV 再生の詳細を直接実装しません。処理は各モジュールへ委譲します。

Win32 標準コントロールを使用し、`CreateWindowExW` で ComboBox、複数行 Edit、Button、状態表示用 Static を生成します。`main_window` は話者一覧の投入、入力テキストと選択 ID の取得、再生要求コールバック、状態表示の更新を担当します。

### 3.2 VoiceLink.TTS

**責務**
- 音声合成エンジンの共通インターフェースを提供する。
- VOICEVOX Engine との HTTP 通信を行う。
- 話者・スタイル情報を取得する。
- テキストを WAV データへ変換する。
- 音声再生デバイスや再生制御を扱わない。

#### `tts_engine`

音声合成エンジンの抽象インターフェースです。

初期案:

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

class tts_engine
{
public:
    virtual ~tts_engine() = default;

    virtual bool initialize() = 0;

    virtual std::vector<std::uint8_t> synthesize(
        const std::string& text,
        int speaker_id
    ) = 0;
};
```

実装時には、初期化・合成失敗の詳細を呼び出し元へ伝える戻り値形式を検討します。
また、話者一覧取得 API と共通の話者・スタイルデータ型を追加します。VOICEVOX 固有の JSON 構造をアプリケーション本体へ直接公開しない方針です。

#### `voicevox_engine`

`tts_engine` を実装し、VOICEVOX Engine の HTTP API を呼び出します。

**主な責務**
- Engine の接続先 URL を保持する。
- Engine への接続確認を行う。
- 話者・スタイル一覧を取得する。
- 音声クエリを作成する。
- 合成 API へ音声クエリを送信する。
- 返却された WAV バイト列を呼び出し元へ返す。

**VOICEVOX API の処理順**

1. `GET /speakers` — 話者とスタイルの一覧を取得。
2. `POST /audio_query?text=...&speaker=...` — テキストから AudioQuery を生成。
3. `POST /synthesis?speaker=...` — AudioQuery の JSON を送信して WAV を取得。

接続先の初期値は `http://127.0.0.1:50021` とします。
ポート番号や URL は将来の設定変更に備えて固定値を各所へ散在させず、一か所で管理します。

VOICEVOX Engine の起動・終了は初期実装の責務に含めません。

### 3.3 VoiceLink.Audio

**責務**
- WAV データを受け取り、音声として再生する。
- 再生停止を行う。
- 再生状態を必要な範囲で提供する。
- TTS エンジンや HTTP 通信に依存しない。

#### `audio_player`

初期案:

```cpp
#pragma once

#include <cstdint>
#include <vector>

class audio_player
{
public:
    virtual ~audio_player() = default;

    virtual bool initialize() = 0;
    virtual bool play(const std::vector<std::uint8_t>& wav_data) = 0;
    virtual void stop() = 0;
};
```

実際の再生 API は、WAV の読み込み方式、非同期再生、停止時の寿命管理を踏まえて決定します。
再生バックエンドとして Windows の XAudio2 等を候補としますが、採用は実装時に確定します。

Audio モジュールは TTS モジュールへ依存しません。WAV データなどの受け渡し可能な形式を介して接続します。

## 4. 初期データフロー

### 4.1 起動・話者一覧取得

```text
main_window
 |
 | initialize / get_speakers
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
 | synthesize(text, speaker_id)
 v
voicevox_engine
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
main_window
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
| VoiceLink / main_window | VoiceLink.TTS | 抽象インターフェース経由で利用する |
| VoiceLink / main_window | VoiceLink.Audio | 音声データを渡して再生を依頼する |
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
| C++ 言語標準 | 未確定 | 利用するコンパイラ・依存ライブラリとの互換性 |
| HTTP クライアント | 未選定 | 導入コスト、同期/非同期、タイムアウト、ライセンス |
| JSON ライブラリ | 未選定 | UTF-8 対応、保守性、ライセンス |
| 音声再生 API | 未確定 | XAudio2 等の再生制御、WAV デコード、非同期処理 |
| TTS の戻り値形式 | 初期案は WAV バイト列 | エラー詳細と結果データの表現 |
| 話者・スタイル型 | 未確定 | 複数スタイル、表示名、ID の扱い |
| 設定保存形式 | 未確定 | 接続先、話者 ID、音量などの保存 |
| VOICEVOX Engine の管理 | 初期は手動起動 | 将来の起動検出・自動起動・終了管理 |

## 9. 現在の実装状況

- Win32 のメインウィンドウと標準コントロールを実装済み。
- 話者一覧を ComboBox に設定する API、選択中の speaker ID を取得する API、テキスト取得 API を用意。
- 再生ボタンはコールバック経由でアプリケーションへ通知する。
- VOICEVOX 接続、話者一覧取得、TTS 合成、音声再生のバックエンドは未実装。現段階の再生ボタンは未接続状態をステータス表示する。

## 10. 更新ルール

- クラスを追加・削除した場合は、該当モジュールの設計を更新する。
- 公開インターフェースを変更した場合は、クラス設計とデータフローを更新する。
- モジュール間の依存関係を変更した場合は、依存関係表を更新する。
- 未決定事項が確定した場合は、決定内容と必要に応じて理由を記録する。
- 実装前の案と実装済みの仕様を混同しない。
