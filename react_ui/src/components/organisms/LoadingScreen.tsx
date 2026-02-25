interface LoadingScreenProps {
  message: string;
  progress: number;
}

export function LoadingScreen({ message, progress }: LoadingScreenProps) {
  return (
    <div className="absolute inset-0 z-100 flex items-center justify-center bg-[hsl(var(--background))] backdrop-blur-md bg-opacity-90">
      <div className="flex flex-col items-center space-y-8 max-w-md w-full text-center px-8">
        <div className="w-12 h-12 rounded-full border-4 border-[hsl(var(--muted))] border-t-[hsl(var(--primary))] border-l-[hsl(var(--primary))] animate-spin shadow-lg" />
        <div className="space-y-4 w-full">
          <h3 className="text-xl font-medium tracking-tight text-[hsl(var(--foreground))]">
            Loading Workspace
          </h3>
          
          <div className="w-full h-1.5 bg-[hsl(var(--muted))] rounded-full overflow-hidden">
            <div 
              className="h-full bg-[hsl(var(--primary))] transition-all duration-200 ease-out" 
              style={{ width: `${Math.max(5, progress * 100)}%` }}
            />
          </div>

          <p className="text-sm font-mono text-[hsl(var(--muted-foreground))] truncate w-full opacity-80">
            {message}
          </p>
        </div>
      </div>
    </div>
  );
}
