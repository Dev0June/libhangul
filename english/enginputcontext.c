#include "enginputcontext.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

/* 크로스 플랫폼 시간 함수 */
#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/time.h>
#endif

/* 함수 선언 */
static char get_right_hand_equivalent(int ascii);
static char get_left_hand_equivalent(int ascii);
static bool is_left_hand_key(int ascii);
static char get_mirror_key_mapping(EngKeyboardType keyboard_type, int ascii);
static void eng_ic_commit_char(EngInputContext* eic, char ch);
static bool eng_ic_process_half_qwerty(EngInputContext* eic, int ascii);
static bool handle_sticky_keys(EngInputContext* eic, int ascii);
static char apply_modifiers(EngInputContext* eic, char ch);

/* 영문 입력 컨텍스트 구조체 */
struct _EngInputContext {
    EngKeyboardType keyboard_type;    // 키보드 타입
    
    /* 공간 효율적인 상태 필드들을 비트필드로 압축 */
    unsigned space_pressed : 1;       // 공백키 상태 (기존 방식)
    unsigned space_down : 1;          // 공백키 다운 상태 (새로운 방식)  
    unsigned space_used : 1;          // Space+키 조합이 사용됨 (공백 출력 방지용)
    unsigned sticky_keys_enabled : 1; // Sticky Keys 기능 활성화
    unsigned shift_sticky : 1;        // Shift 키 Sticky 상태
    unsigned ctrl_sticky : 1;         // Ctrl 키 Sticky 상태  
    unsigned alt_sticky : 1;          // Alt 키 Sticky 상태
    unsigned typing_test_active : 1;  // 타이핑 테스트 활성화
    
    short space_timeout;              // 타임아웃 카운터 (ms) - 최대 32767
    short space_timeout_setting;      // 타임아웃 설정값 (ms, 기본 267)
    
    char commit_string[64];           // 완성된 문자열 (256→64로 축소)
    char commit_length;               // 완성 문자열 길이 (int→char)
    
    /* 타이핑 통계 - 필요시에만 사용 */
    int total_chars;                  // 총 입력 문자 수
    int mirror_chars;                 // 미러 입력 문자 수  
    short errors;                     // 오류 수 (백스페이스) - int→short
    long start_time_ms;               // 시작 시간 (ms)
    long end_time_ms;                 // 종료 시간 (ms)
};

/* 영문 입력 컨텍스트 생성 */
EngInputContext* eng_ic_new(EngKeyboardType keyboard_type)
{
    EngInputContext* eic = (EngInputContext*)malloc(sizeof(EngInputContext));
    if (eic == NULL) return NULL;
    
    eic->keyboard_type = keyboard_type;
    eic->space_pressed = false;
    eic->space_down = false;           // 새로운 필드 초기화
    eic->space_used = false;           // Space+키 사용 여부 초기화
    eic->space_timeout = 0;
    eic->space_timeout_setting = 267;  // 기본 267ms
    eic->commit_length = 0;
    
    /* Sticky Keys 초기화 */
    eic->sticky_keys_enabled = true;  // 기본 활성화
    eic->shift_sticky = false;
    eic->ctrl_sticky = false;
    eic->alt_sticky = false;
    
    /* 타이핑 통계 초기화 */
    eic->typing_test_active = false;
    eic->total_chars = 0;
    eic->mirror_chars = 0;
    eic->errors = 0;
    eic->start_time_ms = 0;
    eic->end_time_ms = 0;
    
    memset(eic->commit_string, 0, sizeof(eic->commit_string));
    
    return eic;
}

/* 영문 입력 컨텍스트 삭제 */
void eng_ic_delete(EngInputContext* eic)
{
    if (eic != NULL) {
        free(eic);
    }
}

/* Half-QWERTY 왼손 키보드 생성 */
EngInputContext* eng_ic_new_half_qwerty_left(void)
{
    return eng_ic_new(ENG_KEYBOARD_TYPE_HALF_QWERTY_LEFT);
}

/* Half-QWERTY 오른손 키보드 생성 */
EngInputContext* eng_ic_new_half_qwerty_right(void)
{
    return eng_ic_new(ENG_KEYBOARD_TYPE_HALF_QWERTY_RIGHT);
}

/* 결과 문자열 */
const char* eng_ic_get_commit_string(EngInputContext* eic)
{
    if (eic == NULL) return "";
    return eic->commit_string;
}

const char* eng_ic_get_preedit_string(EngInputContext* eic)
{
    if (eic == NULL) return "";
    return ""; // preedit는 현재 사용하지 않음
}

/* 상태 관리 */
void eng_ic_reset(EngInputContext* eic)
{
    if (eic == NULL) return;
    
    eic->space_pressed = false;
    eic->space_timeout = 0;
    eic->space_timeout_setting = 267;  // 기본 267ms
    eic->commit_length = 0;
    
    /* Sticky Keys 초기화 */
    eic->sticky_keys_enabled = true;  // 기본 활성화
    eic->shift_sticky = false;
    eic->ctrl_sticky = false;
    eic->alt_sticky = false;
    
    memset(eic->commit_string, 0, sizeof(eic->commit_string));
}

bool eng_ic_is_empty(EngInputContext* eic)
{
    if (eic == NULL) return true;
    return (eic->commit_length == 0 && !eic->space_pressed);
}

/* 설정 */
void eng_ic_set_keyboard_type(EngInputContext* eic, EngKeyboardType type)
{
    if (eic != NULL) {
        eic->keyboard_type = type;
    }
}

EngKeyboardType eng_ic_get_keyboard_type(EngInputContext* eic)
{
    if (eic == NULL) return ENG_KEYBOARD_TYPE_HALF_STANDARD;
    return eic->keyboard_type;
}

/* 타임아웃 설정 */
void eng_ic_set_space_timeout(EngInputContext* eic, int timeout_ms)
{
    if (eic != NULL && timeout_ms >= 50 && timeout_ms <= 1000) {
        // 50ms ~ 1000ms 범위 내에서 설정 가능
        eic->space_timeout_setting = timeout_ms;
    }
}

int eng_ic_get_space_timeout(EngInputContext* eic)
{
    if (eic == NULL) return 267; // 기본값
    return eic->space_timeout_setting;
}

/* Sticky Keys 지원 함수들 */
void eng_ic_set_sticky_keys(EngInputContext* eic, bool enabled)
{
    if (eic != NULL) {
        eic->sticky_keys_enabled = enabled;
        if (!enabled) {
            /* Sticky Keys 비활성화 시 모든 sticky 상태 해제 */
            eic->shift_sticky = false;
            eic->ctrl_sticky = false;
            eic->alt_sticky = false;
        }
    }
}

bool eng_ic_get_sticky_keys(EngInputContext* eic)
{
    if (eic == NULL) return false;
    return eic->sticky_keys_enabled;
}

void eng_ic_set_shift_sticky(EngInputContext* eic, bool sticky)
{
    if (eic != NULL && eic->sticky_keys_enabled) {
        eic->shift_sticky = sticky;
    }
}

void eng_ic_set_ctrl_sticky(EngInputContext* eic, bool sticky)
{
    if (eic != NULL && eic->sticky_keys_enabled) {
        eic->ctrl_sticky = sticky;
    }
}

void eng_ic_set_alt_sticky(EngInputContext* eic, bool sticky)
{
    if (eic != NULL && eic->sticky_keys_enabled) {
        eic->alt_sticky = sticky;
    }
}

/* 현재 시간을 밀리초로 반환 (크로스 플랫폼) */
static long get_current_time_ms(void)
{
#ifdef _WIN32
    /* Windows 구현 */
    FILETIME ft;
    ULARGE_INTEGER ui;
    GetSystemTimeAsFileTime(&ft);
    ui.LowPart = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;
    return (long)(ui.QuadPart / 10000); // 100ns → ms 변환
#else
    /* Unix/Linux/macOS 구현 */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}

/* 타이핑 통계 함수들 */
void eng_ic_start_typing_test(EngInputContext* eic)
{
    if (eic == NULL) return;
    
    eic->typing_test_active = true;
    eic->total_chars = 0;
    eic->mirror_chars = 0;
    eic->errors = 0;
    eic->start_time_ms = get_current_time_ms();
    eic->end_time_ms = 0;
}

void eng_ic_end_typing_test(EngInputContext* eic)
{
    if (eic == NULL || !eic->typing_test_active) return;
    
    eic->end_time_ms = get_current_time_ms();
    eic->typing_test_active = false;
}

EngTypingStats eng_ic_get_typing_stats(EngInputContext* eic)
{
    EngTypingStats stats = {0};
    
    if (eic == NULL) return stats;
    
    stats.total_chars = eic->total_chars;
    stats.mirror_chars = eic->mirror_chars;
    stats.errors = eic->errors;
    stats.start_time_ms = eic->start_time_ms;
    stats.end_time_ms = eic->end_time_ms;
    
    /* WPM 계산 (5문자 = 1단어) */
    if (eic->end_time_ms > eic->start_time_ms) {
        double time_minutes = (eic->end_time_ms - eic->start_time_ms) / 60000.0;
        stats.wpm = (eic->total_chars / 5.0) / time_minutes;
    }
    
    /* 정확도 계산 */
    if (eic->total_chars > 0) {
        stats.accuracy = ((double)(eic->total_chars - eic->errors) / eic->total_chars) * 100.0;
    }
    
    return stats;
}

void eng_ic_reset_typing_stats(EngInputContext* eic)
{
    if (eic == NULL) return;
    
    eic->typing_test_active = false;
    eic->total_chars = 0;
    eic->mirror_chars = 0;
    eic->errors = 0;
    eic->start_time_ms = 0;
    eic->end_time_ms = 0;
}

/* Half-QWERTY 미러 매핑 함수들 */

/* 왼손 → 오른손 매핑 */
static char get_right_hand_equivalent(int ascii)
{
    switch (ascii) {
        case 'q': return 'y';
        case 'w': return 'u';
        case 'e': return 'i';
        case 'r': return 'o';
        case 't': return 'p';
        case 'a': return 'h';
        case 's': return 'j';
        case 'd': return 'k';
        case 'f': return 'l';
        case 'g': return ';';
        case 'z': return 'n';
        case 'x': return 'm';
        case 'c': return ',';
        case 'v': return '.';
        case 'b': return '/';
        
        /* 대문자 처리 */
        case 'Q': return 'Y';
        case 'W': return 'U';
        case 'E': return 'I';
        case 'R': return 'O';
        case 'T': return 'P';
        case 'A': return 'H';
        case 'S': return 'J';
        case 'D': return 'K';
        case 'F': return 'L';
        case 'G': return ':';
        case 'Z': return 'N';
        case 'X': return 'M';
        case 'C': return '<';
        case 'V': return '>';
        case 'B': return '?';
        
        /* 숫자 행 */
        case '1': return '6';
        case '2': return '7';
        case '3': return '8';
        case '4': return '9';
        case '5': return '0';
        
        default: return ascii;
    }
}

/* 오른손 → 왼손 매핑 */
static char get_left_hand_equivalent(int ascii)
{
    switch (ascii) {
        case 'y': return 'q';
        case 'u': return 'w';
        case 'i': return 'e';
        case 'o': return 'r';
        case 'p': return 't';
        case 'h': return 'a';
        case 'j': return 's';
        case 'k': return 'd';
        case 'l': return 'f';
        case ';': return 'g';
        case 'n': return 'z';
        case 'm': return 'x';
        case ',': return 'c';
        case '.': return 'v';
        case '/': return 'b';
        
        /* 대문자 처리 */
        case 'Y': return 'Q';
        case 'U': return 'W';
        case 'I': return 'E';
        case 'O': return 'R';
        case 'P': return 'T';
        case 'H': return 'A';
        case 'J': return 'S';
        case 'K': return 'D';
        case 'L': return 'F';
        case ':': return 'G';
        case 'N': return 'Z';
        case 'M': return 'X';
        case '<': return 'C';
        case '>': return 'V';
        case '?': return 'B';
        
        /* 숫자 행 */
        case '6': return '1';
        case '7': return '2';
        case '8': return '3';
        case '9': return '4';
        case '0': return '5';
        
        default: return ascii;
    }
}

/* 왼손 키인지 판단하는 함수 */
static bool is_left_hand_key(int ascii)
{
    // 왼손 키들 (QWERTY 표준 배치)
    char left_hand_keys[] = "qwertasdfgzxcvb1234567890`-=[]\\;',./~!@#$%^&*()_+{}|:\"<>?";
    
    for (int i = 0; left_hand_keys[i] != '\0'; i++) {
        if (ascii == left_hand_keys[i]) {
            return true;
        }
    }
    
    return false;
}

/* 미러 키 매핑 메인 함수 */
static char get_mirror_key_mapping(EngKeyboardType keyboard_type, int ascii)
{
    switch (keyboard_type) {
        case ENG_KEYBOARD_TYPE_HALF_QWERTY_LEFT:
            return get_right_hand_equivalent(ascii);
        case ENG_KEYBOARD_TYPE_HALF_QWERTY_RIGHT:
            return get_left_hand_equivalent(ascii);
        case ENG_KEYBOARD_TYPE_HALF_STANDARD:
            // 표준 Half-QWERTY: 입력된 키의 반대편 키 반환
            // 왼손 키면 오른손 키를, 오른손 키면 왼손 키를 반환
            if (is_left_hand_key(ascii)) {
                return get_right_hand_equivalent(ascii);
            } else {
                return get_left_hand_equivalent(ascii);
            }
        default:
            return ascii;
    }
}

/* 문자를 commit_string에 추가 */
static void eng_ic_commit_char(EngInputContext* eic, char ch)
{
    if (eic == NULL || eic->commit_length >= 63) return;  // 64→63으로 수정
    
    eic->commit_string[eic->commit_length] = ch;
    eic->commit_length++;
    eic->commit_string[eic->commit_length] = '\0';
}

/* 영문 입력 처리 메인 함수 */
bool eng_ic_process(EngInputContext* eic, int ascii)
{
    if (eic == NULL) return false;
    
    /* 먼저 commit_string 초기화 */
    eic->commit_length = 0;
    memset(eic->commit_string, 0, sizeof(eic->commit_string));
    
    /* 모든 키보드 타입에서 Half-QWERTY 처리 */
    return eng_ic_process_half_qwerty(eic, ascii);
}

/* Sticky Keys 처리 */
static bool handle_sticky_keys(EngInputContext* eic, int ascii)
{
    if (!eic->sticky_keys_enabled) return false;
    
    /* 수정자 키 처리 */
    switch (ascii) {
        case 1:  // Ctrl (가상 키코드)
            eic->ctrl_sticky = !eic->ctrl_sticky;
            return true;
        case 2:  // Shift (가상 키코드) 
            eic->shift_sticky = !eic->shift_sticky;
            return true;
        case 3:  // Alt (가상 키코드)
            eic->alt_sticky = !eic->alt_sticky;
            return true;
    }
    return false;
}

/* 수정자 적용 */
static char apply_modifiers(EngInputContext* eic, char ch)
{
    if (!eic->sticky_keys_enabled) return ch;
    
    /* Shift 적용 (대문자 변환) */
    if (eic->shift_sticky && ch >= 'a' && ch <= 'z') {
        eic->shift_sticky = false; // 한 번 사용 후 해제
        return ch - 'a' + 'A';
    }
    
    /* 특수문자 Shift 변환 */
    if (eic->shift_sticky) {
        eic->shift_sticky = false; // 한 번 사용 후 해제
        switch (ch) {
            case '1': return '!';
            case '2': return '@';
            case '3': return '#';
            case '4': return '$';
            case '5': return '%';
            case '6': return '^';
            case '7': return '&';
            case '8': return '*';
            case '9': return '(';
            case '0': return ')';
            case '-': return '_';
            case '=': return '+';
            case '[': return '{';
            case ']': return '}';
            case '\\': return '|';
            case ';': return ':';
            case '\'': return '"';
            case ',': return '<';
            case '.': return '>';
            case '/': return '?';
            case '`': return '~';
        }
    }
    
    return ch;
}

/* Half-QWERTY 처리 로직 */
static bool eng_ic_process_half_qwerty(EngInputContext* eic, int ascii)
{
    /* 1. Sticky Keys 처리 */
    if (handle_sticky_keys(eic, ascii)) {
        return true;
    }
    
    /* 2. 공백키 처리 */
    if (ascii == ' ') {
        if (eic->space_pressed) {
            /* 공백키가 이미 눌려있으면 공백 출력 */
            char space_char = apply_modifiers(eic, ' ');
            eng_ic_commit_char(eic, space_char);
            eic->space_pressed = false;
            eic->space_timeout = 0;
        } else {
            /* 공백키 눌림 상태로 설정 */
            eic->space_pressed = true;
            eic->space_timeout = eic->space_timeout_setting; // 커스텀 타임아웃
        }
        return true;
    }
    
    /* 3. 일반 키 입력 처리 */
    if (ascii != 0) {  // 유효한 키 입력
        char mapped_char;
        bool is_mirror_input = false;
        
        /* 백스페이스 처리 (오류 카운트) */
        if (ascii == 8 || ascii == 127) {  // 백스페이스
            if (eic->typing_test_active) {
                eic->errors++;
            }
            eng_ic_commit_char(eic, ascii);
            return true;
        }
        
        if (eic->space_pressed) {
            /* 미러 입력: 반대편 키 매핑 */
            mapped_char = get_mirror_key_mapping(eic->keyboard_type, ascii);
            is_mirror_input = true;
            eic->space_pressed = false; // 수정자 해제
            eic->space_timeout = 0;     // 타임아웃 해제
        } else {
            /* 일반 입력: 기본 키 그대로 */
            mapped_char = ascii;
        }
        
        /* Sticky Keys 수정자 적용 */
        mapped_char = apply_modifiers(eic, mapped_char);
        
        /* 타이핑 통계 업데이트 */
        if (eic->typing_test_active && mapped_char >= ' ' && mapped_char <= '~') {
            eic->total_chars++;
            if (is_mirror_input) {
                eic->mirror_chars++;
            }
        }
        
        /* 영문 문자 직접 출력 */
        eng_ic_commit_char(eic, mapped_char);
        return true;
    }
    
    /* 4. 타임아웃 체크 (더미 호출 시) */
    if (eic->space_timeout > 0) {
        eic->space_timeout--;
        if (eic->space_timeout == 0 && eic->space_pressed) {
            /* 타임아웃 → 공백 문자 출력 */
            char space_char = apply_modifiers(eic, ' ');
            eng_ic_commit_char(eic, space_char);
            eic->space_pressed = false;
            return true;
        }
    }
    
    return false;
}

/* 새로운 키 이벤트 처리 함수들 */

/* Space 키 다운/업 상태 설정 */
void eng_ic_set_space_down(EngInputContext* eic, bool down)
{
    if (eic != NULL) {
        eic->space_down = down;
        if (!down) {
            // Space 키를 놓으면 타임아웃도 해제
            eic->space_timeout = 0;
        }
    }
}

/* Space 키 다운 상태 확인 */
bool eng_ic_is_space_down(EngInputContext* eic)
{
    if (eic == NULL) return false;
    return eic->space_down;
}

/* Half-QWERTY 키다운 처리 (개선된 버전) */
static bool eng_ic_process_half_qwerty_keydown(EngInputContext* eic, int ascii);

/* 키 다운 이벤트 처리 */
bool eng_ic_process_key_down(EngInputContext* eic, int ascii)
{
    if (eic == NULL) return false;
    
    /* 먼저 commit_string 초기화 */
    eic->commit_length = 0;
    memset(eic->commit_string, 0, sizeof(eic->commit_string));
    
    /* Space 키 다운 처리 */
    if (ascii == ' ') {
        eic->space_down = true;
        eic->space_used = false;  // Space+키 사용 여부 초기화
        return true; // Space 키 자체는 출력하지 않음
    }
    
    /* 모든 키보드 타입에서 Half-QWERTY 키다운 처리 */
    return eng_ic_process_half_qwerty_keydown(eic, ascii);
}

/* 키 업 이벤트 처리 */
bool eng_ic_process_key_up(EngInputContext* eic, int ascii)
{
    if (eic == NULL) return false;
    
    /* 먼저 commit_string 초기화 */
    eic->commit_length = 0;
    memset(eic->commit_string, 0, sizeof(eic->commit_string));
    
    /* Space 키 업 처리 */
    if (ascii == ' ') {
        if (eic->space_down) {
            eic->space_down = false;
            // Space+키 조합을 사용했다면 공백 출력 안함
            if (!eic->space_used) {
                // Space만 누르고 놓았다면 공백 문자 출력
                char space_char = apply_modifiers(eic, ' ');
                eng_ic_commit_char(eic, space_char);
                return true;
            }
            eic->space_used = false;  // 플래그 리셋
            return true;
        }
    }
    
    return false;
}

/* Half-QWERTY 키다운 처리 (개선된 버전) */
static bool eng_ic_process_half_qwerty_keydown(EngInputContext* eic, int ascii)
{
    /* 1. Sticky Keys 처리 */
    if (handle_sticky_keys(eic, ascii)) {
        return true;
    }
    
    /* 2. 일반 키 입력 처리 */
    if (ascii != 0) {  // 유효한 키 입력
        char mapped_char;
        bool is_mirror_input = false;
        
        /* 백스페이스 처리 (오류 카운트) */
        if (ascii == 8 || ascii == 127) {  // 백스페이스
            if (eic->typing_test_active) {
                eic->errors++;
            }
            eng_ic_commit_char(eic, ascii);
            return true;
        }
        
        if (eic->space_down) {
            /* 미러 입력: 반대편 키 매핑 */
            mapped_char = get_mirror_key_mapping(eic->keyboard_type, ascii);
            is_mirror_input = true;
            eic->space_used = true;  // Space+키 조합 사용됨
            // 주의: Space는 계속 눌려있으므로 해제하지 않음
        } else {
            /* 일반 입력: 기본 키 그대로 */
            mapped_char = ascii;
        }
        
        /* Sticky Keys 수정자 적용 */
        mapped_char = apply_modifiers(eic, mapped_char);
        
        /* 타이핑 통계 업데이트 */
        if (eic->typing_test_active && mapped_char >= ' ' && mapped_char <= '~') {
            eic->total_chars++;
            if (is_mirror_input) {
                eic->mirror_chars++;
            }
        }
        
        /* 영문 문자 직접 출력 */
        eng_ic_commit_char(eic, mapped_char);
        return true;
    }
    
    return false;
}

