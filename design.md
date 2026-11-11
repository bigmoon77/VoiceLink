# VoiceLink Design

このドキュメントは VoiceLink のシステム構成、モジュール境界、クラス設計、データフロー、設計上の決定事項を管理します。
実装の進行に合わせて更新してください。

## 1. システム概要

VoiceLink は Windows 上で動作する音声入出力アプリケーションです。
現在は VOICEVOX / GPT-SoVITS による音声合成と再生、および ReazonSpeech k2-v2 によるローカル音声認識を実装します。SAPI 依存コードは撤去済みです。

VOICEVOX と GPT-SoVITS を利用した Text-to-Speech の変換・再生機能、および ReazonSpeech k2-v2 を利用した音声認識機能を実装しています。ビルド・実機でのマイク認識確認は未完了です。

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
    |      |      +-- gpt_sovits_engine
    |      +-- VOICEVOX Engine (HTTP / localhost)
    |      +-- GPT-SoVITS API (HTTP / localhost)
    |
    +-- VoiceLink.Audio
    |      +-- audio_player
    |
    +-- VoiceLink.STT
    |      +-- speech_recognition_window
    |      +-- audio_recorder (WinMM waveIn)
    |      +-- reazonspeech_engine (sherpa-onnx C API)
    |      +-- ReazonSpeech k2-v2 INT8 model
    |
    +-- voicevox_process
    |      +-- Engine path discovery / process ownership
    +-- [Future] VoiceLink.Browser
           +-- native_messaging_host

Chrome Extension
    +-- ChatGPT tab / input detection
    +-- text injection
```

## 2. プロジェクト構成案

現在は単一の Visual Studio アプリケーションプロジェクトです。Text-to-Speech 関連のソースを `text_to_speech/`、音声認識関連のソースを `speech_to_text/` にまとめ、アプリケーション共通の GUI・設定・エントリーポイントと分離しています。TTS / STT / Audio はディレクトリ単位で責務を分離し、同じアプリケーションプロジェクトに含めます。

```text
VoiceLink/
├── VoiceLink.sln
├── VoiceLink.vcxproj
├── VoiceLink.vcxproj.filters
├── AGENTS.md
├── design.md
├── main.cpp
├── app_settings.h / .cpp
├── storage_system.h / .cpp
├── main_window.h / .cpp
├── speech_to_text/
│   ├── speech_recognition_window.h / .cpp
│   ├── audio_recorder.h / .cpp
│   └── reazonspeech_engine.h / .cpp
├── tools/
│   └── setup_reazonspeech.ps1
├── text_to_speech/
│   ├── tts_controller.h / .cpp
│   ├── tts_engine.h
│   ├── voicevox_engine.h / .cpp
│   ├── gpt_sovits_engine.h / .cpp
│   ├── voicevox_process.h / .cpp
│   └── audio_player.h / .cpp
```

TTS / STT / Audio は現在も単一のアプリケーションプロジェクト内にあります。別プロジェクトや静的ライブラリへの分割は現行方針に含めません。
`storage_system` はアプリケーション共通のユーザーデータ保存窓口です。保存先の解決、ディレクトリ作成、読み書き、一時ファイル、ファイル名の競合回避を担当します。

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
- `tts_engine_combo_box`: VOICEVOX / GPT-SoVITS の選択。
- `engine_path_edit`: VOICEVOX Engine の `run.exe` パス入力。
- `engine_path_browse_button`: `IFileOpenDialog` で実行ファイルを選択。
- `engine_path_apply_button`: 入力パスを保存し、Engine へ再接続。
- `gpt_host_edit` / `gpt_port_edit`: GPT-SoVITS API のホスト名とポート番号。
- `gpt_reference_edit` / `gpt_reference_browse_button`: 参照音声ファイルのパスと選択操作。
- `gpt_prompt_edit`: 参照音声に対応する正解テキスト。
- `gpt_prompt_language_combo` / `gpt_text_language_combo`: 参照音声・生成テキストの言語。
- `gpt_settings_apply_button`: GPT-SoVITS 設定を検証・保存し、API に再接続。

**主な処理**
- `CreateWindowExW` で ComboBox、複数行 Edit、Button、Static を生成する。
- 話者一覧を ComboBox に設定する。
- 選択中の話者 ID と入力テキストを取得する。
- 再生要求と非同期処理結果をコールバック・ウィンドウメッセージで通知する。
- 状態表示と再生ボタンの有効状態を更新する。

HTTP 通信や WAV 再生の詳細は扱わず、`tts_controller` へ委譲します。

#### `call_frequency`

関数ごとの呼び出し回数を計測するヘッダーオンリーのクラスです。実装は `call_frequency.h` に置きます。

**設計**
- `call_frequency<&関数>` のように関数ポインターをテンプレート引数に指定し、関数ごとに独立した静的カウンターを生成します。
- 計測対象関数内では `call_frequency<&対象関数>::increment()` を呼び出します。
- カウンターの更新は `std::atomic<std::uint64_t>::fetch_add(1, std::memory_order_relaxed)` のみです。
- カウンターの識別・保存領域の選択はテンプレートで静的に解決します。実行時の関数名検索、登録テーブル、動的メモリ確保、ロックは使用しません。
- `get_count()` で現在値を取得し、`reset()` で値を0へ戻します。
- `make_entry(L"関数名")` で出力用の関数名とカウンター値を取得します。
- 計測・レポート出力は Debug ビルド限定です。Release ビルドではカウンターを保持せず、`increment()` は空処理、`get_count()` / `reset()` は 0 を返し、`save()` は false を返します。
- `call_frequency_report::save(...)` に出力対象のエントリーを渡すと、CSV をソリューション直下の `call_frequency\\` へ保存します。保存先は Debug 構成の `$(SolutionDir)` から静的に決定します。
- `call_frequency\\` は Git 管理対象です。CSV は UTF-8 BOM 付きで、関数名と累積呼び出し回数を記録します。ファイル名には日時・プロセス ID・連番を含め、既存ファイルを上書きしません。
- 文字列生成・UTF-8 変換・ファイル I/O はレポート保存時だけ実行し、計測対象関数の実行経路には含めません。
- CSV の読み込み機能は設けません。
- `memory_order_relaxed` を使用し、カウンター値の整合性のみを保証します。ほかのデータとの同期には使用しません。

**利用例**
```cpp
void update()
{
    call_frequency<&update>::increment();
}

const std::uint64_t count = call_frequency<&update>::get_count();

// 計測結果の保存
call_frequency_report::save(
    call_frequency<&update>::make_entry(L"update"),
    call_frequency<&render>::make_entry(L"render")
);
```

メンバー関数も `call_frequency<&class_name::method_name>` として計測できます。オーバーロード関数は関数ポインター型を明示して選択します。

計測対象への `increment()` の追加は明示的に行います。自動登録や全関数への自動計測は行いません。出力対象の関数は `save()` の引数として明示します。テンプレート引数を自動列挙する仕組みや実行時登録テーブルは設けません。

#### `storage_system`

アプリケーション共通のファイル保存窓口です。`storage_system.h / .cpp` に実装します。

**責務**
- `%APPDATA%\\VoiceLink\\` と用途別ディレクトリのパス解決・作成。
- `config.json` の固定パス解決。
- バイナリ / UTF-8 テキストの読み書き。
- 一意な一時ファイルを使った安全な更新。
- 排他的な新規作成、ファイル名の競合回避。

GUI や各機能クラスは保存先を直接組み立てず、このクラスの API を利用します。データ形式の解析や設定値の意味付けは各機能側に残し、ストレージ層は汎用的なデータ入出力を担当します。

#### `app_settings`

`%APPDATA%\\VoiceLink\\config.json` を読み書きします。VOICEVOX の実行ファイルパスに加え、選択中の TTS エンジンと GPT-SoVITS の接続先・参照音声・参照テキスト・言語設定を保持します。JSON は UTF-8 で保存します。

#### アプリケーションデータの保存規約

ユーザーデータのルートは `%APPDATA%\\VoiceLink\\` とします。保存先の解決とファイル I/O は `storage_system` を経由します。保存先・形式・ファイル名規則を追加するときは、この節を先に更新し、実装と一致させます。各機能が独自にルートパスや用途別ディレクトリを組み立てないでください。

##### ディレクトリレイアウト

| パス | 用途 | 状態 |
|---|---|---|
| `%APPDATA%\\VoiceLink\\config.json` | アプリケーション設定 | 実装済み |
| `call_frequency\\`（ソリューション直下） | Debug ビルドの呼び出し頻度レポート CSV。Git 管理対象 | 実装済み |
| `%APPDATA%\\VoiceLink\\cache\\` | 再生成可能な一時データ・キャッシュ | 必要になった段階で追加 |

設定ファイルは現状ルート直下に置きます。用途別ディレクトリを追加する場合は、既存ファイルを理由なく移動せず、移行が必要なら旧パスからの読み込みと移行手順を別途定義します。

GPT-SoVITS の参照音声は設定に絶対パスを保持し、VoiceLink のデータ領域へコピーしません。参照音声の実体はユーザーが選択した外部ファイルです。

##### ファイル形式

| データ | 形式・文字コード | 内容・規則 |
|---|---|---|
| `config.json` | JSON / UTF-8（BOM なし） | TTS エンジン選択、VOICEVOX パス、GPT-SoVITS 接続先・参照音声・テキスト・言語 |
| 呼び出し頻度レポート | CSV / UTF-8（BOM 付き） | 関数名と累積呼び出し回数。読み込み機能なし。ファイル名は日時・プロセス ID・Tick Count を含む |


##### ファイル名と衝突回避

- 固定名を使う設定・履歴ファイルは、用途ごとに一つだけ配置します。設定は更新対象、履歴は追記対象として扱います。
- 新しい生成物には用途が分かる接頭辞を付け、異なる機能間で同じ固定ファイル名を共有しません。
- ファイル名の重複確認だけに依存しません。実際の新規作成は排他的な作成（Windows の `CREATE_NEW` 相当）で行い、作成競合が起きたら別名を採番して再試行します。
- 既存ファイルの更新は、同じディレクトリ内の一意な一時ファイルへ完全に書き込んでから置き換えます。追記専用の履歴ファイルはこの置換方式を使いません。
- 一時ファイルは完成品と区別できる拡張子（例: `.tmp`）を使い、通常の読み込み対象に含めません。起動時または保存時に残存一時ファイルをどう扱うかは、導入時に明示します。

##### 保存処理と競合回避

`storage_system` は以下を提供します。

- ルート・用途別ディレクトリと固定ファイルパスの解決、必要なディレクトリの作成。
- バイナリ / UTF-8 テキストの読み込み。
- 新規ファイルの `CREATE_NEW` による排他的作成。
- 固定ファイル更新時のプロセス ID・時刻・連番を含む一意な一時ファイルへの書き込み、フラッシュ後の `MoveFileExW` による置換。
- UTF-8 BOM の有無を用途ごとに指定した追記。
- 呼び出し頻度レポートの UTF-8 BOM 付き新規ファイル保存。


現行実装上の制約:

- `config.json` は一意な一時ファイルへ保存後に置換します。複数プロセスから同時更新された場合の内容マージや更新順序の調停は行いません。
- SAPI 関連の旧ユーザーデータ（`sapi_training.txt`、`training_samples`）は自動削除しません。現行アプリケーションからは読み込み・更新しません。


#### `tts_controller`

GUI と TTS / Audio の間を仲介するクラスです。実装ファイルは `text_to_speech/` に配置しています。

**責務**
- GUI で選択された TTS エンジンへ接続し、話者・スタイル一覧を取得する。
- VOICEVOX と GPT-SoVITS のエンジンを切り替え、共通の `tts_engine` 経由で合成する。
- `main_window` から読み上げテキストと speaker ID を受け取る。
- `tts_engine` に音声合成を依頼する。
- 合成結果の WAV を `audio_player` へ渡す。
- 接続状態、処理中、成功、失敗を GUI に反映する。
- HTTP 通信・合成処理をワーカースレッドで実行し、完了を `PostMessageW` で UI スレッドへ通知する。

同時に複数の合成要求を実行せず、処理中は再生ボタンを無効化します。UI コントロールの操作は UI スレッド上で行います。

### 3.2 VoiceLink.STT

**責務**
- マイクから音声を録音し、日本語音声をローカルで文字列へ変換する。
- 録音、推論、認識 UI を分離する。
- SAPI には依存しない。

#### `audio_recorder`

WinMM `waveIn` を利用する録音クラスです。

- 入力形式は 16 kHz / mono / PCM16。
- `CALLBACK_WINDOW` で録音バッファの完了をウィンドウメッセージとして受け取り、再登録する。
- PCM16 を -1.0 ～ 1.0 の float サンプルへ変換する。
- 最大録音時間は25秒。ReazonSpeech k2-v2 の入力上限（約30秒）を超えないよう余裕を持たせる。
- 録音バッファの停止・リセット・解放を UI スレッド上で完了させる。

#### `reazonspeech_engine`

sherpa-onnx C API を動的ロードし、ReazonSpeech k2-v2 を実行するクラスです。

- sherpa-onnx v2.0.1 の Windows x64 shared MD Release DLL を使用する。
- ReazonSpeech k2-v2 の INT8 Zipformer Transducer モデルを使用する。
- モデルファイルは `models/reazonspeech-k2-v2/`、DLL は `third_party/sherpa-onnx/` に配置する。
- CPU provider、4スレッド、greedy search でオフライン認識する。GPU は使用しない。
- sherpa-onnx の DLL は `LoadLibraryExW` で遅延ロードし、C API 関数を `GetProcAddress` で取得する。
- 入力音声を OfflineStream へ一度だけ渡し、推論後に UTF-8 の認識結果を UTF-16 へ変換する。
- x64 ビルドのみ対応する。Win32 ビルドでは未対応エラーを返す。

#### `speech_recognition_window`

音声認識用の独立した Win32 ウィンドウです。

- 初回表示時にモデルをワーカースレッドで初期化する。
- 録音開始・停止ボタンでマイク音声を取得し、停止後に別ワーカースレッドで推論する。
- 認識結果は編集可能な複数行 Edit に表示する。
- 「読み上げ欄へ反映」でメインウィンドウの TTS 入力欄へ認識結果を渡す。
- モデル初期化と推論中は UI スレッドをブロックしない。
- 録音は最大25秒。ストリーミング認識ではなく、発話終了後に一括で推論する。

#### 依存ライブラリ・モデルの取得

- sherpa-onnx: v2.0.1 Windows x64 shared MD Release（TTS 無効）。
- ReazonSpeech k2-v2: INT8 encoder / decoder / joiner と `tokens.txt`。
- 取得スクリプト: `tools/setup_reazonspeech.ps1`。
- 依存バイナリとモデル本体は容量が大きいため Git 管理対象外とする。各開発環境でスクリプトを実行して取得する。
- sherpa-onnx と ReazonSpeech k2-v2 のライセンスは Apache-2.0。

### 3.3 VoiceLink.TTS

**責務**
- 音声合成エンジンの共通インターフェースを提供する。
- VOICEVOX Engine / GPT-SoVITS API との HTTP 通信を行う。
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

VOICEVOX Engine の起動・終了は voicevox_process が管理します。GUI で指定された `run.exe` パスを優先し、未指定の場合は標準インストール先と新しいエンジン管理フォルダーから自動検索します。未起動の場合だけ起動します。VoiceLink が起動したプロセスは Job Object で所有し、VoiceLink 終了時に停止します。既存の Engine には接続するだけで、終了処理の対象にしません。

#### `gpt_sovits_engine`

`tts_engine` を実装し、GPT-SoVITS の `api_v2.py` が公開する HTTP API を呼び出します。

- 初期接続先は `http://127.0.0.1:9880`。
- `GET /openapi.json` で API の起動状態を確認します。
- `POST /tts` に JSON を送信し、WAV データを取得します。
- リクエストには合成テキスト、言語、参照音声ファイルパス、参照音声のテキストを指定します。
- GPT-SoVITS は VOICEVOX のような話者一覧を提供しないため、共通インターフェースには参照音声を表す単一の仮想スタイルを返します。
- 参照音声・参照テキスト・言語・接続先は `gpt_sovits_engine::settings` で受け取ります。
- メイン GUI の GPT-SoVITS 設定欄からホスト、ポート、参照音声、参照テキスト、参照言語、生成言語を編集できます。
- 参照音声はファイル選択ダイアログで指定します。参照テキストと参照音声の言語はユーザーが対応を確認して設定します。
- 言語選択肢は `ja` / `en` / `zh` / `ko` / `yue` / `auto` です。
- Apply 操作で必須項目とポート範囲を検証し、`app_settings` に保存してから `tts_controller` に設定を反映し、再接続します。
- GPT-SoVITS は単一の仮想スタイルを返すため、VOICEVOX の話者一覧 UI は共通インターフェース維持用の選択肢として表示されます。
- API プロセスの起動・終了は VoiceLink から管理せず、外部で起動済みの API に接続します。

### 3.4 VoiceLink.Audio

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

選択中のエンジンに応じて接続先を切り替えます。

- VOICEVOX: `voicevox_process` で必要に応じて Engine を起動し、`GET /speakers` で実際の話者・スタイル一覧を取得します。
- GPT-SoVITS: 外部起動済み API の `GET /openapi.json` で接続を確認し、参照音声を表す単一の仮想スタイルを返します。

どちらも `tts_engine::speaker_style` に変換し、`tts_controller` から UI スレッドへ通知します。

### 4.2 テキスト読み上げ

`tts_controller` は共通の `tts_engine` を呼び出します。

- VOICEVOX: `/audio_query` → `/synthesis` の順で WAV を取得します。
- GPT-SoVITS: 設定済みの参照音声・参照テキスト・言語と生成テキストを `POST /tts` に送り、WAV を取得します。

取得した WAV は共通の `audio_player` へ渡します。

### 4.3 音声認識

1. メインウィンドウの「音声認識」ボタンから `speech_recognition_window` を開く。
2. 初回表示時に `reazonspeech_engine` が sherpa-onnx DLL と INT8 モデルをワーカースレッドで初期化する。
3. 「録音開始」で `audio_recorder` が WinMM `waveIn` を開始し、16 kHz / mono / PCM16 の入力を float サンプルへ変換する。
4. 「録音停止」または25秒経過で録音を終了する。
5. `reazonspeech_engine` が OfflineStream へ音声全体を渡し、CPU で一括推論する。
6. UTF-8 の認識結果を UTF-16 へ変換して UI に表示する。
7. ユーザーが必要に応じて結果を編集し、「読み上げ欄へ反映」でメインウィンドウのテキスト入力欄へ渡す。

モデルはオフライン認識専用のため、録音中の逐次テキスト更新は行いません。

## 5. 依存関係のルール

| 呼び出し元 | 依存先 | 方針 |
|---|---|---|
| VoiceLink / main_window | tts_controller | TTS GUI の入力と非同期結果を仲介する |
| VoiceLink / main_window | speech_recognition_window | 音声認識ウィンドウの表示と認識結果の反映 |
| speech_recognition_window | audio_recorder | マイク録音と PCM サンプルの取得 |
| speech_recognition_window | reazonspeech_engine | モデル初期化と非同期推論 |
| reazonspeech_engine | sherpa-onnx C API | OfflineStream を使った音声認識 |
| tts_controller | tts_engine | 抽象インターフェース経由で合成を依頼する |
| tts_controller | audio_player | WAV データを渡して再生を依頼する |
| VoiceLink.TTS | HTTP クライアント / JSON ライブラリ | VOICEVOX 通信の内部実装として利用する |
| VoiceLink.Audio | 再生バックエンド | Audio 内部に閉じ込める |
| VoiceLink.Audio | VoiceLink.TTS | 依存させない |
| VoiceLink.TTS | VoiceLink.Audio | 依存させない |

モジュール間の依存は原則として一方向にします。循環依存を作らないでください。

## 6. 将来機能の設計方針

### 6.1 SAPI の撤去

VoiceLink では SAPI による音声認識・学習機能を撤去しました。SAPI 認識エンジン、旧 STT コントローラー、旧音声認識インターフェース、旧録音・訂正履歴・サンプル管理・適応処理は現行構成に含めません。

現在の音声認識は 3.2 節の ReazonSpeech k2-v2 と sherpa-onnx を使用します。新規コードへ SAPI を追加しません。

### 6.2 ChatGPT Web 連携

ブラウザー連携は、C++ アプリケーションと Chrome 拡張機能の間を Native Messaging で接続する構成を基本案とします。

- C++ 側: Native Messaging Host としてメッセージを受け渡す。
- 拡張機能側: ChatGPT の対象タブと入力欄を検出し、テキストを反映する。
- TTS モジュールは、ブラウザー DOM を直接操作しない。

フォーカスを奪わずに入力することを重要な要件とします。実現可能なブラウザー API と ChatGPT Web 側の挙動を検証しながら詳細を確定します。

## 7. エラー処理方針

初期実装で少なくとも次の失敗を区別します。

- VOICEVOX Engine または GPT-SoVITS API に接続できない。
- GPT-SoVITS の参照音声ファイルが存在しない、参照テキストが未設定である、または接続先設定が不正である。
- HTTP リクエストが失敗する、またはタイムアウトする。
- Engine がエラー応答を返す。
- JSON の解析に失敗する。
- 指定した話者・スタイルが存在しない。
- WAV データが空、または不正である。
- 音声デバイスの初期化・再生に失敗する。
- マイクを開けない、録音バッファを登録できない、または録音を停止できない。
- sherpa-onnx DLL / ONNX Runtime DLL / ReazonSpeech モデルが見つからない、または読み込めない。
- ReazonSpeech の推論に失敗する、録音時間が短すぎる、またはモデルの入力長を超える。

エラーの詳細を保持し、アプリケーション側でログ表示やユーザー通知を行える設計にします。

## 8. 未決定事項

以下は実装前または実装時に検証して決定します。

| 項目 | 現状 | 決定時の観点 |
|---|---|---|
| GUI フレームワーク | **決定: Win32 標準コントロール** | Windows API の ComboBox / Edit / Button / Static を使用。追加の GUI 依存なし |
| C++ 言語標準 | **決定: Win32 は C++17、x64 は C++20** | Visual Studio の構成ごとの設定に合わせる |
| HTTP クライアント | **決定: WinHTTP** | Windows 標準 API を利用 |
| JSON パーサー | **決定: Windows.Data.Json (C++/WinRT)** | Windows SDK の JSON API を利用 |
| 音声再生 API | **初期実装: WinMM PlaySoundW** | WAV メモリ再生。高度な制御が必要になれば XAudio2 等を検討 |
| 音声認識 API | **決定: sherpa-onnx C API + ReazonSpeech k2-v2** | Windows x64、CPU INT8、OfflineStream による最大25秒の一括認識 |
| TTS の戻り値形式 | `bool` + WAV バイト列 + エラー文字列 | `tts_engine` の共通インターフェースで統一 |
| 話者・スタイル型 | `tts_engine::speaker_style` | 話者名、スタイル名、speaker ID を保持 |
| 設定保存形式 | **決定: JSON (`%APPDATA%\\VoiceLink\\config.json`)** | 選択中のエンジン、VOICEVOX の実行ファイルパス、GPT-SoVITS の接続先・参照音声・参照テキスト・言語設定を保存 |
| VOICEVOX Engine の管理 | **決定: voicevox_process で自動管理** | 標準パスと `%APPDATA%\\voicevox\\vvpp-engines` を検索。既存 Engine は再利用し、自分で起動したプロセスだけ終了時に停止 |

## 9. 現在の実装状況

- Win32 のメインウィンドウと標準コントロールを実装済み。
- `tts_engine` 抽象インターフェースと VOICEVOX 実装を追加済み。
- VOICEVOX は WinHTTP で `/speakers`、`/audio_query`、`/synthesis` を呼び出す。
- GPT-SoVITS 用の `gpt_sovits_engine` を追加し、`/openapi.json` による接続確認と `POST /tts` による WAV 取得を実装。
- `tts_controller` が GUI 入力、非同期 TTS 処理、結果通知、音声再生を仲介する。
- `audio_player` が WAV をメモリから非同期再生する。
- VOICEVOX 接続先は現在 `http://127.0.0.1:50021` 固定。GPT-SoVITS は設定可能なホスト・ポートを利用する。`voicevox_process` が Engine の検索・起動・終了管理を行う。
- GUI から TTS エンジンを選択でき、選択内容を `%APPDATA%\\VoiceLink\\config.json` に保存する。
- GPT-SoVITS 設定欄から接続先、参照音声、参照テキスト、参照言語、生成言語を編集・保存し、設定反映後に再接続する。
- GUI から `run.exe` のパスを指定でき、同じ設定ファイルに保存する。
- Text-to-Speech 関連ソースは `text_to_speech/` に配置し、単一のアプリケーションプロジェクトに含める。
- SAPI 音声認識・トレーニング機能は撤去済み。`speech_to_text/` に WinMM 録音、sherpa-onnx C API、ReazonSpeech k2-v2 の認識 UI を追加。
- `tools/setup_reazonspeech.ps1` で sherpa-onnx v2.0.1 Windows x64 DLL と ReazonSpeech k2-v2 INT8 モデルを取得する。DLL・モデル本体は Git 管理対象外。
- ReazonSpeech の音声認識は CPU で動作し、録音は最大25秒。モデル初期化・推論はワーカースレッドで実行する。
- sherpa-onnx CLI による公式テスト音声の認識確認済み。VoiceLink アプリのビルド・マイク録音・UI 経由の認識テストは未確認。
- Windows / MSVC での VoiceLink アプリのビルド・TTS 実機テストは未確認。ReazonSpeech は sherpa-onnx CLI で公式テスト音声の認識を確認済み。

## 10. 更新ルール

- クラスを追加・削除した場合は、該当モジュールの設計を更新する。
- 公開インターフェースを変更した場合は、クラス設計とデータフローを更新する。
- モジュール間の依存関係を変更した場合は、依存関係表を更新する。
- 未決定事項が確定した場合は、決定内容と必要に応じて理由を記録する。
- 実装前の案と実装済みの仕様を混同しない。
