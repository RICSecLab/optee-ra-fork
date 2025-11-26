#![allow(clippy::missing_safety_doc)]

use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_uchar, c_void};

#[repr(C)]
pub struct ChallengeResponseSession {
    pub session_url: *mut c_char,
    pub accept_type_count: usize,
    pub accept_type_list: *mut *mut c_char,
    pub nonce: *mut c_uchar,
    pub nonce_size: usize,
    pub message: *mut c_char,
    pub attestation_result: *mut c_char,
}

#[repr(C)]
pub enum VeraisonResult {
    Ok = 0,
    Error = 1,
}

unsafe fn strdup_rust(s: &str) -> *mut c_char {
    CString::new(s).unwrap().into_raw()
}

#[cfg(not(feature = "real"))]
#[no_mangle]
pub unsafe extern "C" fn open_challenge_response_session(
    endpoint: *const c_char,
    desired_nonce_size: usize,
    _accept_type: *const c_char,
    out_session: *mut *mut ChallengeResponseSession,
) -> VeraisonResult {
    if endpoint.is_null() || out_session.is_null() {
        return VeraisonResult::Error;
    }
    let endpoint_c = CStr::from_ptr(endpoint).to_string_lossy().into_owned();
    let mut sess = Box::new(ChallengeResponseSession {
        session_url: strdup_rust(&endpoint_c),
        accept_type_count: 1,
        accept_type_list: std::ptr::null_mut(),
        nonce: std::ptr::null_mut(),
        nonce_size: if desired_nonce_size == 0 { 32 } else { desired_nonce_size },
        message: std::ptr::null_mut(),
        attestation_result: std::ptr::null_mut(),
    });

    // accept types (PSA by default)
    let accept = strdup_rust("application/psa-attestation-token");
    let list = libc::calloc(1, std::mem::size_of::<*mut c_char>()) as *mut *mut c_char;
    if list.is_null() {
        return VeraisonResult::Error;
    }
    *list = accept;
    sess.accept_type_list = list;

    // nonce (stub: all zeros)
    let nonce = libc::calloc(sess.nonce_size, 1) as *mut c_uchar;
    if nonce.is_null() {
        return VeraisonResult::Error;
    }
    sess.nonce = nonce;

    *out_session = Box::into_raw(sess);
    VeraisonResult::Ok
}

#[cfg(feature = "real")]
#[no_mangle]
pub unsafe extern "C" fn open_challenge_response_session(
    endpoint: *const c_char,
    desired_nonce_size: usize,
    _accept_type: *const c_char,
    out_session: *mut *mut ChallengeResponseSession,
) -> VeraisonResult {
    use veraison_apiclient as api;

    if endpoint.is_null() || out_session.is_null() {
        return VeraisonResult::Error;
    }

    let endpoint_str = CStr::from_ptr(endpoint).to_string_lossy().into_owned();
    let nonce_size = if desired_nonce_size == 0 { 32 } else { desired_nonce_size };

    // Build Veraison API client
    let cr = match api::ChallengeResponseBuilder::new()
        .with_new_session_url(endpoint_str.clone())
        .build() {
        Ok(cr) => cr,
        Err(e) => {
            eprintln!("Failed to build ChallengeResponse client: {:?}", e);
            return VeraisonResult::Error;
        }
    };

    // Create tokio runtime for async call
    let rt = match tokio::runtime::Builder::new_current_thread().enable_all().build() {
        Ok(rt) => rt,
        Err(e) => {
            eprintln!("Failed to create tokio runtime: {:?}", e);
            return VeraisonResult::Error;
        }
    };

    // Call new_session to get nonce from server
    // Note: new_session returns (String, ChallengeResponseSession)
    let nonce = api::Nonce::Size(nonce_size);
    let new_sess_result = rt.block_on(cr.new_session(&nonce));
    let (session_url, api_session) = match new_sess_result {
        Ok(s) => s,
        Err(e) => {
            eprintln!("Failed to create new session with Veraison: {:?}", e);
            return VeraisonResult::Error;
        }
    };

    let mut sess = Box::new(ChallengeResponseSession {
        session_url: strdup_rust(&session_url),
        accept_type_count: api_session.accept().len(),
        accept_type_list: std::ptr::null_mut(),
        nonce: std::ptr::null_mut(),
        nonce_size: api_session.nonce().len(),
        message: std::ptr::null_mut(),
        attestation_result: std::ptr::null_mut(),
    });

    // Copy accept types
    let list = libc::calloc(sess.accept_type_count, std::mem::size_of::<*mut c_char>()) as *mut *mut c_char;
    if list.is_null() {
        return VeraisonResult::Error;
    }
    for (i, accept_type) in api_session.accept().iter().enumerate() {
        *list.add(i) = strdup_rust(accept_type);
    }
    sess.accept_type_list = list;

    // Copy nonce from server
    let nonce = libc::malloc(sess.nonce_size) as *mut c_uchar;
    if nonce.is_null() {
        return VeraisonResult::Error;
    }
    std::ptr::copy_nonoverlapping(api_session.nonce().as_ptr(), nonce, sess.nonce_size);
    sess.nonce = nonce;

    *out_session = Box::into_raw(sess);
    VeraisonResult::Ok
}

#[no_mangle]
pub unsafe extern "C" fn free_challenge_response_session(session: *mut ChallengeResponseSession) {
    if session.is_null() { return; }
    let sess = Box::from_raw(session);
    if !sess.accept_type_list.is_null() {
        for i in 0..sess.accept_type_count {
            let p = *sess.accept_type_list.add(i);
            if !p.is_null() { let _ = CString::from_raw(p); }
        }
        libc::free(sess.accept_type_list as *mut c_void);
    }
    if !sess.nonce.is_null() { libc::free(sess.nonce as *mut c_void); }
    if !sess.session_url.is_null() { let _ = CString::from_raw(sess.session_url); }
    if !sess.message.is_null() { let _ = CString::from_raw(sess.message); }
    if !sess.attestation_result.is_null() { let _ = CString::from_raw(sess.attestation_result); }
}

#[cfg(not(feature = "real"))]
#[no_mangle]
pub unsafe extern "C" fn challenge_response(
    session: *mut ChallengeResponseSession,
    evidence_len: usize,
    _evidence: *const c_uchar,
    _media_type: *const c_char,
) -> VeraisonResult {
    if session.is_null() || evidence_len == 0 { return VeraisonResult::Error; }
    let sess = &mut *session;
    sess.attestation_result = strdup_rust("eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9...stub");
    VeraisonResult::Ok
}

#[cfg(feature = "real")]
#[no_mangle]
pub unsafe extern "C" fn challenge_response(
    session: *mut ChallengeResponseSession,
    evidence_len: usize,
    evidence: *const c_uchar,
    media_type: *const c_char,
) -> VeraisonResult {
    use veraison_apiclient as api;
    if session.is_null() || evidence_len == 0 || evidence.is_null() || media_type.is_null() {
        return VeraisonResult::Error;
    }
    let sess = &mut *session;
    if sess.session_url.is_null() { return VeraisonResult::Error; }
    let mt = CStr::from_ptr(media_type).to_string_lossy().into_owned();
    let url = CStr::from_ptr(sess.session_url).to_string_lossy().into_owned();
    let evid = std::slice::from_raw_parts(evidence, evidence_len);

    // Build a minimal ChallengeResponse client (builder needs any absolute URL; we pass session URL)
    let cr = match api::ChallengeResponseBuilder::new()
        .with_new_session_url(url.clone())
        .build() {
        Ok(cr) => cr,
        Err(_) => return VeraisonResult::Error,
    };

    // Run the async call synchronously
    let rt = match tokio::runtime::Builder::new_current_thread().enable_all().build() {
        Ok(rt) => rt,
        Err(_) => return VeraisonResult::Error,
    };
    let res = rt.block_on(cr.challenge_response(evid, &mt, &url));
    match res {
        Ok(jwt) => { sess.attestation_result = strdup_rust(&jwt); VeraisonResult::Ok }
        Err(_) => VeraisonResult::Error,
    }
}


