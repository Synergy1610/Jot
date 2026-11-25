-- v2: one cloud pad per authenticated user.
-- The app remains local-first; this table is only used when signed in.

create table if not exists public.pads (
    user_id uuid primary key references auth.users(id) on delete cascade,
    content text not null default '',
    updated_at timestamptz not null default now(),
    version bigint not null default 1
);

grant usage on schema public to authenticated;
grant select, insert, update, delete on public.pads to authenticated;

alter table public.pads enable row level security;

create policy "Users can read their own pad"
    on public.pads for select to authenticated
    using (auth.uid() = user_id);

create policy "Users can insert their own pad"
    on public.pads for insert to authenticated
    with check (auth.uid() = user_id);

create policy "Users can update their own pad"
    on public.pads for update to authenticated
    using (auth.uid() = user_id)
    with check (auth.uid() = user_id);
