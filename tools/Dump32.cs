// Read-only, out-of-process x86 minidump writer for the 32-bit game.
using System;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

class Dump32 {
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern IntPtr OpenProcess(uint rights, bool inherit, uint pid);
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr handle);
    [DllImport("dbghelp.dll", SetLastError=true)]
    static extern bool MiniDumpWriteDump(IntPtr process, uint pid, SafeFileHandle file,
        uint type, IntPtr exception, IntPtr streams, IntPtr callback);

    static int Main(string[] args) {
        IntPtr process=IntPtr.Zero;
        try {
            if(IntPtr.Size!=4 || args.Length!=2) throw new Exception("Usage: Dump32.exe <pid> <new dump path>; x86 build required");
            uint pid=UInt32.Parse(args[0]);
            process=OpenProcess(0x410,false,pid); // PROCESS_QUERY_INFORMATION | PROCESS_VM_READ
            if(process==IntPtr.Zero) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            using(var file=new FileStream(args[1],FileMode.CreateNew,FileAccess.Write,FileShare.None)) {
                if(!MiniDumpWriteDump(process,pid,file.SafeFileHandle,0x1000,IntPtr.Zero,IntPtr.Zero,IntPtr.Zero))
                    throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
                file.Flush(true);
            }
            Console.WriteLine("x86 thread/stack dump saved"); return 0;
        } catch(Exception error) { Console.Error.WriteLine(error.Message); return 1; }
        finally { if(process!=IntPtr.Zero) CloseHandle(process); }
    }
}
