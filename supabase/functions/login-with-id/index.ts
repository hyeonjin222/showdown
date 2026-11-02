// 아이디(username) + 비밀번호로 로그인하는 Edge Function.
//
// 왜 Edge Function인가:
//  - Supabase Auth는 이메일/전화로만 로그인할 수 있어, 아이디로 로그인하려면
//    아이디 -> 이메일 매핑이 필요합니다.
//  - 그 매핑을 클라이언트에서 하면 이메일이 응답으로 노출되므로(누구나 anon 키로 호출 가능),
//    매핑과 로그인을 전부 서버(이 함수) 안에서 끝내고 "세션"만 돌려줍니다.
//  - service_role 키는 이 함수(서버)에만 존재하고 클라이언트에는 절대 나가지 않습니다.
//
// 게임 클라이언트와 웹사이트(GitHub Pages) 모두 이 함수를 호출해 공용으로 씁니다.

import { createClient } from "https://esm.sh/@supabase/supabase-js@2";

// 웹(브라우저)에서 호출할 때 필요한 CORS 헤더. 게임 클라이언트(Unreal HTTP)는 신경 안 씀.
const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "authorization, x-client-info, apikey, content-type",
  "Access-Control-Allow-Methods": "POST, OPTIONS",
};

function jsonResponse(body: unknown, status: number): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { ...corsHeaders, "Content-Type": "application/json" },
  });
}

Deno.serve(async (req) => {
  // 브라우저 preflight 처리
  if (req.method === "OPTIONS") {
    return new Response("ok", { headers: corsHeaders });
  }

  if (req.method !== "POST") {
    return jsonResponse({ error: "Method not allowed." }, 405);
  }

  try {
    const { id, password } = await req.json();

    if (!id || !password) {
      return jsonResponse({ error: "Missing id or password." }, 400);
    }

    const SUPABASE_URL = Deno.env.get("SUPABASE_URL")!;
    const SERVICE_ROLE_KEY = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!;
    const ANON_KEY = Deno.env.get("SUPABASE_ANON_KEY")!;

    // service_role: RLS를 우회해 profiles를 읽고 auth admin API를 쓰기 위함
    const admin = createClient(SUPABASE_URL, SERVICE_ROLE_KEY);

    // 1) 아이디(username) -> auth 유저 id
    const { data: profile, error: profileError } = await admin
      .from("profiles")
      .select("id")
      .eq("username", id)
      .maybeSingle();

    // 아이디 존재 여부를 노출하지 않도록 실패 메시지는 항상 동일하게("Invalid credentials")
    if (profileError || !profile) {
      return jsonResponse({ error: "Invalid credentials." }, 401);
    }

    // 2) auth 유저 id -> 이메일 (이 이메일은 함수 밖으로 절대 나가지 않음)
    const { data: userData, error: userError } =
      await admin.auth.admin.getUserById(profile.id);

    if (userError || !userData?.user?.email) {
      return jsonResponse({ error: "Invalid credentials." }, 401);
    }

    const email = userData.user.email;

    // 3) 이메일 + 비밀번호로 실제 로그인 (anon 클라이언트로 세션 발급)
    const anon = createClient(SUPABASE_URL, ANON_KEY);
    const { data: signInData, error: signInError } =
      await anon.auth.signInWithPassword({ email, password });

    // 로그인 횟수 제한(기본 5분당 30회)에 걸린 경우는 비밀번호 오류와 구분해서 알려줍니다.
    // 이 함수는 모든 유저의 로그인을 서버 한 곳에서 대신하므로, 시연장처럼 로그인이 몰리면 제한에 걸릴 수 있습니다.
    if (signInError?.status === 429) {
      return jsonResponse({ error: "Too many login attempts." }, 429);
    }

    if (signInError || !signInData?.session) {
      return jsonResponse({ error: "Invalid credentials." }, 401);
    }

    // 4) 세션만 반환 (이메일은 포함하지 않음)
    return jsonResponse(
      {
        access_token: signInData.session.access_token,
        refresh_token: signInData.session.refresh_token,
        expires_in: signInData.session.expires_in,
        user_id: signInData.user.id,
      },
      200,
    );
  } catch (_e) {
    return jsonResponse({ error: "Server error." }, 500);
  }
});
