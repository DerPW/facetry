// Autosave (#17): the open document in the native format (ADR-0008), kept in browser storage so
// that a closed or crashed tab loses at most the last few seconds of work. The core worker
// writes it; the UI offers to recover autosaves whose tab is gone.

/** One autosaved document. */
export interface AutosaveEntry {
  /** The tab that saved it (see {@link tabLock}). */
  id: string;
  /** The document's file name. */
  name: string;
  /** When it was saved, in milliseconds since 1970. */
  savedAt: number;
}

export interface AutosaveStore {
  /** "opfs" or "indexeddb". */
  readonly kind: string;
  list(): Promise<AutosaveEntry[]>;
  read(id: string): Promise<Uint8Array | null>;
  write(entry: AutosaveEntry, bytes: Uint8Array): Promise<void>;
  remove(id: string): Promise<void>;
}

/** The Web Lock a tab holds while it is open, so other tabs leave its autosave alone. */
export const tabLock = (id: string) => `facetry-tab-${id}`;

/**
 * The autosaves to offer for recovery: those whose tab no longer holds its lock, newest first.
 * `held` are the names of the locks held right now.
 */
export function recoverable(entries: AutosaveEntry[], held: ReadonlySet<string>): AutosaveEntry[] {
  return entries
    .filter((entry) => !held.has(tabLock(entry.id)))
    .sort((a, b) => b.savedAt - a.savedAt);
}

const isEntry = (value: unknown): value is AutosaveEntry =>
  typeof value === "object" &&
  value !== null &&
  typeof (value as AutosaveEntry).id === "string" &&
  typeof (value as AutosaveEntry).name === "string" &&
  typeof (value as AutosaveEntry).savedAt === "number";

// Parts of the origin private file system that TypeScript's DOM library leaves out.
interface SyncAccessHandle {
  truncate(size: number): void;
  write(buffer: Uint8Array, options?: { at?: number }): number;
  flush(): void;
  close(): void;
}
type WorkerFileHandle = FileSystemFileHandle & {
  createSyncAccessHandle(): Promise<SyncAccessHandle>;
};
type ListableDirectory = FileSystemDirectoryHandle & { keys(): AsyncIterableIterator<string> };

/** Files in the origin private file system: `<id>.facetry` and its `<id>.json`. Workers only. */
class OpfsStore implements AutosaveStore {
  readonly kind = "opfs";
  readonly #directory: ListableDirectory;

  constructor(directory: FileSystemDirectoryHandle) {
    this.#directory = directory as ListableDirectory;
  }

  async list(): Promise<AutosaveEntry[]> {
    const entries: AutosaveEntry[] = [];
    for await (const name of this.#directory.keys()) {
      if (!name.endsWith(".json")) continue;
      try {
        const file = await (await this.#directory.getFileHandle(name)).getFile();
        const entry: unknown = JSON.parse(await file.text());
        if (isEntry(entry)) entries.push(entry);
      } catch {
        // Removed meanwhile by another tab, or damaged: not recoverable.
      }
    }
    return entries;
  }

  async read(id: string): Promise<Uint8Array | null> {
    try {
      const file = await (await this.#directory.getFileHandle(`${id}.facetry`)).getFile();
      return new Uint8Array(await file.arrayBuffer());
    } catch {
      return null;
    }
  }

  async write(entry: AutosaveEntry, bytes: Uint8Array): Promise<void> {
    // The document first: its entry only appears once there is something to recover.
    await this.#writeFile(`${entry.id}.facetry`, bytes);
    await this.#writeFile(`${entry.id}.json`, new TextEncoder().encode(JSON.stringify(entry)));
  }

  async remove(id: string): Promise<void> {
    for (const name of [`${id}.json`, `${id}.facetry`]) {
      try {
        await this.#directory.removeEntry(name);
      } catch {
        // Not there.
      }
    }
  }

  async #writeFile(name: string, bytes: Uint8Array): Promise<void> {
    const file = (await this.#directory.getFileHandle(name, { create: true })) as WorkerFileHandle;
    const access = await file.createSyncAccessHandle();
    try {
      access.truncate(0);
      access.write(bytes, { at: 0 });
      access.flush();
    } finally {
      access.close();
    }
  }
}

/** One IndexedDB object store with the entry and the document's bytes together. */
class IndexedDbStore implements AutosaveStore {
  readonly kind = "indexeddb";
  readonly #db: IDBDatabase;

  private constructor(db: IDBDatabase) {
    this.#db = db;
  }

  static async open(): Promise<IndexedDbStore> {
    const request = indexedDB.open("facetry", 1);
    request.onupgradeneeded = () => request.result.createObjectStore("autosave", { keyPath: "id" });
    return new IndexedDbStore(await done(request));
  }

  async list(): Promise<AutosaveEntry[]> {
    const records = await done(this.#store("readonly").getAll());
    return (records as unknown[])
      .filter(isEntry)
      .map(({ id, name, savedAt }) => ({ id, name, savedAt }));
  }

  async read(id: string): Promise<Uint8Array | null> {
    const record = (await done(this.#store("readonly").get(id))) as { bytes?: unknown } | undefined;
    return record?.bytes instanceof Uint8Array ? record.bytes : null;
  }

  async write(entry: AutosaveEntry, bytes: Uint8Array): Promise<void> {
    await done(this.#store("readwrite").put({ ...entry, bytes }));
  }

  async remove(id: string): Promise<void> {
    await done(this.#store("readwrite").delete(id));
  }

  #store(mode: IDBTransactionMode): IDBObjectStore {
    return this.#db.transaction("autosave", mode).objectStore("autosave");
  }
}

function done<T>(request: IDBRequest<T>): Promise<T> {
  return new Promise((resolve, reject) => {
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error ?? new Error("IndexedDB request failed"));
  });
}

/**
 * The best store this browser offers: the origin private file system where a worker can write
 * it, IndexedDB otherwise. Null when there is no storage, e.g. in some private windows.
 */
export async function openAutosaveStore(): Promise<AutosaveStore | null> {
  try {
    if (
      typeof FileSystemFileHandle !== "undefined" &&
      "createSyncAccessHandle" in FileSystemFileHandle.prototype
    ) {
      const root = await navigator.storage.getDirectory();
      return new OpfsStore(await root.getDirectoryHandle("autosave", { create: true }));
    }
  } catch {
    // Not allowed here; try IndexedDB.
  }
  try {
    if (typeof indexedDB !== "undefined") return await IndexedDbStore.open();
  } catch {
    // Not allowed either.
  }
  return null;
}
